#!/usr/bin/env python3
"""Regression check of linker selection only; does not emulate CANN or NPU kernels."""
import pathlib
import shutil
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    modules = tmp / 'modules'
    project = tmp / 'project'
    libs = tmp / 'libs'
    for d in (modules, project / 'kernel', project / 'src', libs):
        d.mkdir(parents=True)
    cxx = shutil.which('c++')
    assert cxx and shutil.which('cmake')
    (modules / 'FindASC.cmake').write_text('set(ASC_FOUND TRUE)\n')
    (modules / 'CMakeDetermineASCCompiler.cmake').write_text(f'''
set(CMAKE_ASC_COMPILER "{cxx}")
set(CMAKE_ASC_COMPILER_ENV_VAR "ASC")
set(CMAKE_ASC_COMPILER_ID "MockASC")
set(CMAKE_ASC_COMPILER_LOADED TRUE)
set(CMAKE_ASC_COMPILER_WORKS TRUE)
set(CMAKE_ASC_SOURCE_FILE_EXTENSIONS asc)
set(CMAKE_ASC_OUTPUT_EXTENSION .o)
set(CMAKE_ASC_LINKER_PREFERENCE 1)
file(WRITE "${{CMAKE_PLATFORM_INFO_DIR}}/CMakeASCCompiler.cmake"
  "set(CMAKE_ASC_COMPILER {cxx})\\nset(CMAKE_ASC_COMPILER_LOADED TRUE)\\nset(CMAKE_ASC_SOURCE_FILE_EXTENSIONS asc)\\nset(CMAKE_ASC_OUTPUT_EXTENSION .o)\\n")
''')
    (modules / 'CMakeTestASCCompiler.cmake').write_text('set(CMAKE_ASC_COMPILER_WORKS TRUE)\n')
    (modules / 'CMakeASCInformation.cmake').write_text(f'''
set(CMAKE_ASC_COMPILE_OBJECT "{cxx} -x c++ -fPIC <DEFINES> <INCLUDES> -c <SOURCE> -o <OBJECT>")
set(CMAKE_ASC_CREATE_SHARED_LIBRARY "{cxx} -shared -Wl,--as-needed -L{libs} <OBJECTS> <LINK_LIBRARIES> -o <TARGET>")
set(CMAKE_ASC_LINKER_PREFERENCE 1)
''')
    shutil.copy(root / 'kernel/CMakeLists.txt', project / 'kernel/CMakeLists.txt')
    shutil.copy(root / 'kernel/prl_timing.cpp', project / 'kernel/prl_timing.cpp')
    for name in ('prl_timing.c', 'prl_timing.h'):
        shutil.copy(root / 'src' / name, project / 'src' / name)
    for n in ('cube', 'vec'):
        (project / 'kernel' / f'pearl_hlc2_{n}.asc').write_text(f'int mock_{n}() {{ return 0; }}\n')
    empty = tmp / 'empty.cpp'
    empty.write_text('int mock_library() { return 0; }\n')
    for n in ('tiling_api', 'register', 'platform'):
        subprocess.run([cxx, '-shared', '-fPIC', str(empty), '-o', str(libs / f'lib{n}.so')], check=True)
    build = tmp / 'build'
    subprocess.run(['cmake', '-S', str(project / 'kernel'), '-B', str(build),
                    '-DCMAKE_MODULE_PATH=' + str(modules)], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['cmake', '--build', str(build), '-j2'], check=True, stdout=subprocess.DEVNULL)
    command = (build / 'CMakeFiles/pearl_hlc2.dir/link.txt').read_text()
    assert '-L' + str(libs) in command, command
    assert 'prl_timing.cpp.o' in command, command
    assert (build / 'libpearl_hlc2.so').exists()
    # Negative control: without the explicit language, CXX wins and loses the
    # mock ASC-only library path, reproducing the reported missing-library error.
    cmake_file = project / 'kernel/CMakeLists.txt'
    cmake_file.write_text(cmake_file.read_text().replace('    LINKER_LANGUAGE ASC\n', ''))
    negative = tmp / 'negative'
    subprocess.run(['cmake', '-S', str(project / 'kernel'), '-B', str(negative),
                    '-DCMAKE_MODULE_PATH=' + str(modules)], check=True, stdout=subprocess.DEVNULL)
    failed = subprocess.run(['cmake', '--build', str(negative), '-j2'],
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert failed.returncode != 0 and 'cannot find -ltiling_api' in failed.stdout, failed.stdout
    print('PASS: without the fix, CXX linker reproduces the missing-library error.')
    print('PASS: mixed ASC/CXX target uses explicit ASC link rule; shared library includes logger object.')
    print('Mock rule and dummy kernels only; this is not a CANN build.')
