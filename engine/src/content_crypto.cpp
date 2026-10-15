// Independent desktop content encryption adapter; no third-party crypto implementation.
#include <pybind11/embed.h>
#include <stdexcept>
#include <string>
#ifdef __APPLE__
#include <CommonCrypto/CommonCryptor.h>
#elif defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#endif

namespace {
// The packager replaces only this fixed-size slot before relocation/signing.
// Volatile keeps the bytes in storage rather than compiler-folded constants.
static volatile char contentKey[] = "FORGE-CONTENT-KEY\0"
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    "\0FORGE-CONTENT-END";
std::string packageKey() {
    std::string key(64,'\0');
    for(size_t i=0;i<key.size();++i)key[i]=contentKey[sizeof("FORGE-CONTENT-KEY")+i];
    return key;
}
std::string aesCbc(const std::string& input, const std::string& key, const std::string& iv, bool encrypt) {
    if(key.size()!=32 || iv.size()!=16 || input.size()>1024*1024+16)
        throw std::runtime_error("Invalid protected content cipher parameters");
    std::string output(input.size()+16,'\0');
#ifdef __APPLE__
    size_t written=0;
    auto status=CCCrypt(encrypt?kCCEncrypt:kCCDecrypt,kCCAlgorithmAES,kCCOptionPKCS7Padding,
        key.data(),key.size(),iv.data(),input.data(),input.size(),output.data(),output.size(),&written);
    if(status!=kCCSuccess)throw std::runtime_error("Protected content AES operation failed");
    output.resize(written);
#elif defined(_WIN32)
    struct Algorithm {
        BCRYPT_ALG_HANDLE handle=nullptr;
        ~Algorithm(){if(handle)BCryptCloseAlgorithmProvider(handle,0);}
    } algorithm;
    struct Key {
        BCRYPT_KEY_HANDLE handle=nullptr;
        ~Key(){if(handle)BCryptDestroyKey(handle);}
    } secret;
    auto checked=[](NTSTATUS status){if(status<0)throw std::runtime_error("Protected content AES operation failed");};
    checked(BCryptOpenAlgorithmProvider(&algorithm.handle,BCRYPT_AES_ALGORITHM,nullptr,0));
    checked(BCryptSetProperty(algorithm.handle,BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),sizeof(BCRYPT_CHAIN_MODE_CBC),0));
    checked(BCryptGenerateSymmetricKey(algorithm.handle,&secret.handle,nullptr,0,
        reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())),static_cast<ULONG>(key.size()),0));
    std::string mutableIv=iv;ULONG written=0;
    auto operation=encrypt?BCryptEncrypt:BCryptDecrypt;
    checked(operation(secret.handle,reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
        static_cast<ULONG>(input.size()),nullptr,reinterpret_cast<PUCHAR>(mutableIv.data()),16,
        reinterpret_cast<PUCHAR>(output.data()),static_cast<ULONG>(output.size()),&written,BCRYPT_BLOCK_PADDING));
    output.resize(written);
#else
    (void)encrypt;
    throw std::runtime_error("Protected content builds require macOS or Windows");
#endif
    return output;
}
}
PYBIND11_EMBEDDED_MODULE(_forge_content_crypto, module) {
    module.def("package_key",[](){return pybind11::bytes(packageKey());});
    module.def("aes_cbc",[](pybind11::bytes data,pybind11::bytes key,pybind11::bytes iv,bool encrypt){
        return pybind11::bytes(aesCbc(data,key,iv,encrypt));
    });
}
