@echo off

for %%f in (*.*) do (
  if "%%~xf"==".rgen" (
    glslangValidator -S rgen %%~f --target-env vulkan1.2 -o %%~f.spv
  )
  if "%%~xf"==".rmiss" (
    glslangValidator -S rmiss %%~f --target-env vulkan1.2 -o %%~f.spv
  )
  if "%%~xf"==".rchit" (
    glslangValidator -S rchit %%~f --target-env vulkan1.2 -o %%~f.spv
  )
  if "%%~xf"==".rahit" (
    glslangValidator -S rahit %%~f --target-env vulkan1.2 -o %%~f.spv
  )
  if "%%~xf"==".rint" (
    glslangValidator -S rint %%~f --target-env vulkan1.2 -o %%~f.spv
  )
  if "%%~xf"==".comp" (
    glslangValidator -S comp %%~f --target-env vulkan1.2 -o %%~f.spv
  )

)
@echo on
