@echo off
set COMPILE_FLAGS=-spirv -WX -O3 -fvk-use-dx-layout -enable-16bit-types
set PIVOT_DIRECTORY=.\..\..\..

@echo on
"%ANDROID_VULKAN_DXC_ROOT%\dxc.exe" %COMPILE_FLAGS% -T vs_6_6 -E VS -I %PIVOT_DIRECTORY%\hlsl -Fo %PIVOT_DIRECTORY%\assets\shaders\%1-vs.spv %PIVOT_DIRECTORY%\hlsl\%1.vs

@echo off
echo Done
