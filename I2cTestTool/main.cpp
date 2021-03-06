/*
Copyright(c) Microsoft Open Technologies, Inc. All rights reserved.

The MIT License(MIT)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

//
// i2ctesttool
//
//   Utility to read and write I2C devices from the command line.
//   Shows how to use C++/CX in console applications.
//

#include <ppltasks.h>
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <cwctype>

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Devices::I2c;

class wexception
{
public:
    explicit wexception (const std::wstring &msg) : msg_(msg) { }
    virtual ~wexception () { /*empty*/ }

    virtual const wchar_t *wwhat () const
    {
        return msg_.c_str();
    }

private:
    std::wstring msg_;
};

I2cDevice^ MakeDevice (int slaveAddress, _In_opt_ String^ friendlyName)
{
    using namespace Windows::Devices::Enumeration;

    String^ aqs;
    if (friendlyName)
        aqs = I2cDevice::GetDeviceSelector(friendlyName);
    else
        aqs = I2cDevice::GetDeviceSelector();

    auto dis = concurrency::create_task(DeviceInformation::FindAllAsync(aqs)).get();
    if (dis->Size != 1) {
        throw wexception(L"I2C bus not found");
    }

    String^ id = dis->GetAt(0)->Id;
    auto device = concurrency::create_task(I2cDevice::FromIdAsync(
                    id,
                    ref new I2cConnectionSettings(slaveAddress))).get();

    if (!device) {
        std::wostringstream msg;
        msg << L"Slave address 0x" << std::hex << slaveAddress << L" on bus " << id->Data() <<
            L" is in use. Please ensure that no other applications are using I2C.";
        throw wexception(msg.str());
    }

    return device;
}

std::wostream& operator<< (std::wostream& os, const I2cTransferResult& result)
{
    switch (result.Status) {
    case I2cTransferStatus::FullTransfer: break;
    case I2cTransferStatus::PartialTransfer:
        os << L"Partial Transfer. Transferred " <<
            result.BytesTransferred << L" bytes\n";
        break;
    case I2cTransferStatus::SlaveAddressNotAcknowledged:
        os << L"Slave address was not acknowledged\n";
        break;
    default:
        throw wexception(L"Invalid transfer status value");
    }
    return os;
}

std::wistream& expect (std::wistream& is, wchar_t delim)
{
    wchar_t ch;
    while (is.get(ch)) {
        if (ch == delim) return is;
        if (!isspace(ch)) {
            is.clear(is.failbit);
            break;
        }
    }
    return is;
}

std::wistream& operator>> (std::wistream& is, std::vector<BYTE>& bytes)
{
    bytes.clear();

    if (!expect(is, L'{')) {
        std::wcout << L"Syntax error: expecting '{'\n";
        return is;
    }

    // get a sequence of bytes, e.g.
    //   write { 0 1 2 3 4 aa bb cc dd }
    unsigned int byte;
    while (is >> std::hex >> byte) {
        if (byte > 0xff) {
            std::wcout << L"Out of range [0, 0xff]: " << std::hex << byte << L"\n";
            is.clear(is.failbit);
            return is;
        }
        bytes.push_back(static_cast<BYTE>(byte));
    }

    if (bytes.empty()) {
        std::wcout << L"Zero-length buffers are not allowed\n";
        is.clear(is.failbit);
        return is;
    }

    is.clear();
    if (!expect(is, L'}')) {
        std::wcout << L"Syntax error: expecting '}'\n";
        return is;
    }
    return is;
}

std::wostream& operator<< (std::wostream& os, const Platform::Array<BYTE>^ bytes)
{
    for (auto byte : bytes)
        os << L" " << std::hex << byte;
    return os;
}

std::wostream& operator<< (std::wostream& os, I2cBusSpeed busSpeed)
{
    switch (busSpeed) {
    case I2cBusSpeed::StandardMode:
        return os << L"StandardMode (100Khz)";
    case I2cBusSpeed::FastMode:
        return os << L"FastMode (400kHz)";
    default:
        return os << L"[Invalid bus speed]";
    }
}

PCWSTR Help =
    L"Commands:\n"
    L" > write { 00 11 22 .. FF }         Write bytes to device\n"
    L" > read N                           Read N bytes\n"
    L" > writeread { 00 11 .. FF } N      Write bytes, restart, read N bytes\n"
    L" > info                             Display device information\n"
    L" > help                             Display this help message\n"
    L" > quit                             Quit\n\n";

void ShowPrompt (I2cDevice^ device)
{
    while (std::wcin) {
        std::wcout << L"> ";

        std::wstring line;
        if (!std::getline(std::wcin, line)) {
            return;
        }
        std::wistringstream linestream(line);

        std::wstring command;
        linestream >> command;
        if ((command == L"q") || (command == L"quit")) {
            return;
        } else if ((command == L"h") || (command == L"help")) {
            std::wcout << Help;
        } else if (command == L"write") {
            std::vector<BYTE> writeBuf;
            if (!(linestream >> writeBuf)) {
                std::wcout << L"Usage: write { 55 a0 ... ff }\n";
                continue;
            }

            I2cTransferResult result = device->WritePartial(
                ArrayReference<BYTE>(
                    writeBuf.data(),
                    static_cast<unsigned int>(writeBuf.size())));

            switch (result.Status) {
            case I2cTransferStatus::FullTransfer:
                break;
            case I2cTransferStatus::PartialTransfer:
                std::wcout << L"Partial Transfer. Transferred " <<
                    result.BytesTransferred << L" bytes\n";
                break;
            case I2cTransferStatus::SlaveAddressNotAcknowledged:
                std::wcout << L"Slave address was not acknowledged\n";
                break;
            default:
                throw wexception(L"Invalid transfer status value");
            }
        } else if (command == L"read") {
            // expecting a single int, number of bytes to read
            unsigned int bytesToRead;
            if (!(linestream >> std::dec >> bytesToRead)) {
                std::wcout << L"Expecting integer. e.g: read 4\n";
                continue;
            }

            auto readBuf = ref new Platform::Array<BYTE>(bytesToRead);
            I2cTransferResult result = device->ReadPartial(readBuf);

            switch (result.Status) {
            case I2cTransferStatus::FullTransfer:
                std::wcout << readBuf << L"\n";
                break;
            case I2cTransferStatus::PartialTransfer:
                std::wcout << L"Partial Transfer. Transferred " <<
                    result.BytesTransferred << L" bytes\n";
                std::wcout << readBuf << L"\n";
                break;
            case I2cTransferStatus::SlaveAddressNotAcknowledged:
                std::wcout << L"Slave address was not acknowledged\n";
                break;
            default:
                throw wexception(L"Invalid transfer status value");
            }
        } else if (command == L"writeread") {
            // get a sequence of bytes, e.g.
            //   write 0 1 2 3 4 aa bb cc dd
            std::vector<BYTE> writeBuf;
            if (!(linestream >> writeBuf)) {
                std::wcout << L"Usage: writeread { 55 a0 ... ff } 4\n";
                continue;
            }

            unsigned int bytesToRead;
            if (!(linestream >> std::dec >> bytesToRead)) {
                std::wcout << L"Syntax error: expecting integer\n";
                std::wcout << L"Usage: writeread { 55 a0 ... ff } 4\n";
                continue;
            }
            auto readBuf = ref new Array<BYTE>(bytesToRead);

            I2cTransferResult result = device->WriteReadPartial(
                ArrayReference<BYTE>(
                    writeBuf.data(),
                    static_cast<unsigned int>(writeBuf.size())),
                readBuf);

            switch (result.Status) {
            case I2cTransferStatus::FullTransfer:
                std::wcout << readBuf << L"\n";
                break;
            case I2cTransferStatus::PartialTransfer:
            {
                std::wcout << L"Partial Transfer. Transferred " <<
                    result.BytesTransferred << L" bytes\n";
                int bytesRead = result.BytesTransferred - int(writeBuf.size());
                if (bytesRead > 0) {
                    std::wcout << readBuf << L"\n";
                }
                break;
            }
            case I2cTransferStatus::SlaveAddressNotAcknowledged:
                std::wcout << L"Slave address was not acknowledged\n";
                break;
            default:
                throw wexception(L"Invalid transfer status value");
            }
        } else if (command == L"info") {
            int slaveAddress = device->ConnectionSettings->SlaveAddress;
            I2cBusSpeed busSpeed = device->ConnectionSettings->BusSpeed;

            std::wcout << L"       DeviceId: " << device->DeviceId->Data() << "\n";
            std::wcout << L"  Slave address: 0x" << std::hex << slaveAddress << L"\n";
            std::wcout << L"      Bus Speed: " << busSpeed << L"\n";
        } else if (command.empty()) {
            // ignore
        } else {
            std::wcout << L"Unrecognized command: " << command <<
                L". Type 'help' for command usage.\n";
        }
    }
}

void PrintUsage (PCWSTR name)
{
    wprintf(
        L"I2cTestTool: Command line I2C testing utility\n"
        L"Usage: %s SlaveAddress [FriendlyName]\n"
        L"\n"
        L"  SlaveAddress   The slave address of the device with which you\n"
        L"                 wish to communicate. This is a required parameter.\n"
        L"  FriendlyName   The friendly name of the I2C controller over\n"
        L"                 which you wish to communicate. This parameter is\n"
        L"                 optional and defaults to the first enumerated\n"
        L"                 I2C controller.\n"
        L"\n"
        L"Examples:\n"
        L"  %s 0x57\n"
        L"  %s 0x57 I2C1\n",
        name,
        name,
        name);
}

int main (Platform::Array<Platform::String^>^ args)
{
    unsigned int optind = 1;
    if (optind < args->Length) {
        if ((args->get(optind) == L"-h") || (args->get(optind) == L"-?")) {
            PrintUsage(args->get(0)->Data());
            return 0;
        }
    } else {
        std::wcerr << L"Missing required command line parameter SlaveAddress\n\n";
        PrintUsage(args->get(0)->Data());
        return 1;
    }

    int slaveAddress;
    {
        String^ arg = args->get(optind++);
        wchar_t *endptr;
        slaveAddress = int(wcstoul(arg->Data(), &endptr, 0));
        if (endptr != arg->End()) {
            std::wcerr << L"Expecting integer: " << arg->Data() << L"\n";
            std::wcerr << L"Type '" << args->get(0)->Data() << " -h' for usage\n";
            return 1;
        }
    }

    String^ friendlyName;
    if (optind < args->Length) {
        friendlyName = args->get(optind++);
    }

    try {
        auto device = MakeDevice(slaveAddress, friendlyName);

        std::wcout << L"  Type 'help' for a list of commands\n";
        ShowPrompt(device);
    } catch (const wexception& ex) {
        std::wcerr << L"Error: " << ex.wwhat() << L"\n";
        return 1;
    } catch (Platform::Exception^ ex) {
        std::wcerr << L"Error: " << ex->Message->Data() << L"\n";
        return 1;
    }

    return 0;
}
