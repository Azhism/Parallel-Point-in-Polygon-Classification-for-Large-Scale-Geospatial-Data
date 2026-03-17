@echo off
REM Build script for Milestone 1 - Point-in-Polygon Classification
REM This is a simple direct compilation without CMake

setlocal enabledelayedexpansion

set CXXFLAGS=/O2 /EHsc /std:c++17 /I"include" /Wall /WX-

echo [*] Compiling core library...

REM Compile source files
cl.exe %CXXFLAGS% /c src/geometry/point.cpp /Fo"build/point.obj"
if errorlevel 1 goto error

cl.exe %CXXFLAGS% /c src/geometry/polygon.cpp /Fo"build/polygon.obj"
if errorlevel 1 goto error

cl.exe %CXXFLAGS% /c src/geometry/ray_casting.cpp /Fo"build/ray_casting.obj"
if errorlevel 1 goto error

cl.exe %CXXFLAGS% /c src/index/rtree_index.cpp /Fo"build/rtree_index.obj"
if errorlevel 1 goto error

cl.exe %CXXFLAGS% /c src/index/bbox_filter.cpp /Fo"build/bbox_filter.obj"
if errorlevel 1 goto error

cl.exe %CXXFLAGS% /c src/generator/uniform_distribution.cpp /Fo"build/uniform_distribution.obj"
if errorlevel 1 goto error

cl.exe %CXXFLAGS% /c src/generator/clustered_distribution.cpp /Fo"build/clustered_distribution.obj"
if errorlevel 1 goto error

cl.exe %CXXFLAGS% /c src/generator/polygon_loader.cpp /Fo"build/polygon_loader.obj"
if errorlevel 1 goto error

echo [*] Linking library...
lib.exe /out:build/pip_core.lib build/point.obj build/polygon.obj build/ray_casting.obj build/rtree_index.obj build/bbox_filter.obj build/uniform_distribution.obj build/clustered_distribution.obj build/polygon_loader.obj
if errorlevel 1 goto error

echo [*] Compiling unit tests...
cl.exe %CXXFLAGS% tests/test_ray_casting.cpp build/pip_core.lib /Fe"build/test_ray_casting.exe"
if errorlevel 1 goto error

echo [*] Compiling benchmark...
cl.exe %CXXFLAGS% src/benchmark_m1.cpp build/pip_core.lib /Fe"build/benchmark_m1.exe"
if errorlevel 1 goto error

echo [*] Build completed successfully!
echo Run: build\test_ray_casting.exe
echo Run: build\benchmark_m1.exe
goto end

:error
echo ERROR: Build failed!
exit /b 1

:end
