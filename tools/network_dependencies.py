#!/usr/bin/env python3
"""Build pinned private Protobuf/OpenSSL for optional Valve sockets; no global installation."""
from pathlib import Path
import argparse, os, subprocess, sys
import dependencies
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix',type=Path,default=ROOT/'.tools/network')
    parser.add_argument('--deployment-target',default='11.0',help='macOS minimum deployment target')
    args=parser.parse_args();prefix=args.prefix.resolve()
    from forge import cmake_path
    cmake=cmake_path()
    # Reuse TLS, SHA-256 and safe archive extraction from the standard bootstrap.
    subprocess.run([sys.executable,ROOT/'tools/dependencies.py','--sockets-deps','--without-editor','--without-directx','--without-metal'],check=True)
    environment=dict(os.environ)
    if sys.platform=="darwin":environment["MACOSX_DEPLOYMENT_TARGET"]=args.deployment_target
    def run(command,cwd=ROOT):subprocess.run(list(map(str,command)),cwd=cwd,env=environment,check=True)
    if sys.platform!='win32':
        source=ROOT/'vendor/openssl'
        flags=['no-shared','no-tests',f'--prefix={prefix}']
        if sys.platform=='darwin':flags += [f'-mmacosx-version-min={args.deployment_target}']
        run(['perl','Configure',*flags],source)
        run(['make','-j',str(min(os.cpu_count() or 2,4))],source)
        run(['make','install_sw'],source)
    # A static protobuf library still needs Forge/GNS's dynamic MSVC CRT (/MD).
    # Protobuf defaults to /MT for static libraries, which otherwise produces
    # LNK2038 RuntimeLibrary mismatches in the Windows socket build.
    flags=['-DCMAKE_BUILD_TYPE=Release','-Dprotobuf_BUILD_TESTS=OFF','-Dprotobuf_BUILD_SHARED_LIBS=OFF','-Dprotobuf_MSVC_STATIC_RUNTIME=OFF','-Dprotobuf_WITH_ZLIB=OFF',f'-DCMAKE_INSTALL_PREFIX={prefix}']
    if sys.platform=='darwin':flags += [f'-DCMAKE_OSX_DEPLOYMENT_TARGET={args.deployment_target}']
    build=ROOT/'build/network-protobuf'
    run([cmake,'-S',ROOT/'vendor/protobuf/cmake','-B',build,*flags])
    run([cmake,'--build',build,'--config','Release','--parallel','4'])
    run([cmake,'--install',build,'--config','Release'])
    print(f'Private dependencies ready: {prefix}')
    print('Configure Forge with --sockets and CMAKE_PREFIX_PATH pointing to this directory.')


if __name__=='__main__':main()
