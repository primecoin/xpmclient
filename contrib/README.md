How to deploy xpmclient:

1. Install docker (on Windows 10 also needed msys2 or different bash terminal)
2. Download CUDA 11.8 for Windows: https://developer.nvidia.com/cuda-11-8-0-download-archive?target_os=Windows&target_arch=x86_64&target_version=10&target_type=exe_local and put it to contrib directory
3. Setup CUDA10_INSTALLER variable in deploy.sh file:

  CUDA10_INSTALLER="cuda_11.8.0_522.06_windows.exe"
  
4. run deploy.sh
