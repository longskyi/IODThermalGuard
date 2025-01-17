# IODThermalGuard
### Important Notice
**Please note that this software is currently intended for the author's personal use only and is not recommended for deployment in production environments.**  
**DO NOT deploy this software in a production environment**. The software may not function correctly during Windows sleep states or other similar scenarios. **Use at your own risk.**
### Short Description
__IODThermalGuard__ is an open-source software solution designed to manage AMD CPU's I/O Die chiplet (IOD) overheating issues in the ASUS ROG G733PY/PZ/PYV (ROG 魔霸7plus超能版) laptop series.

The overheating issues of the IOD might cause the laptop to shut down. The author believes that the root cause of IOD overheating is the excessive heat generated by the GPU under high load conditions, which may adversely affect the CPU.



### How Does This Project Work?
By leveraging NVIDIA's NVML library and shared memory data from HWINFO64, __IODThermalGuard__ monitors the temperature of the IOD on the CPU during intensive tasks, such as gaming and rendering.

The software intelligently adjusts the NVIDIA GPU's maximum graphics clock limit based on real-time IOD temperature readings, preventing overheating and system shutdown.

### How to Build
IODThermalGuard is built using CMake and supports both GCC and MSVC compilers. It is developed in C++20. To build this software, you need to add `nvml.h` and `nvml.lib` to the `lib` directory. You can download these files from the [NVIDIA Developer website](https://developer.nvidia.com/management-library-nvml).

Please note that this software can only be built and used on Windows. Currently, there is no known method to obtain AMD CPU IOD temperature readings without utilizing HWINFO64. 

### How to Use
1. **Install HWINFO64**: Download and install the latest version of HWINFO64.

2. **Configure HWINFO64**:
   - Open HWINFO64 and enable shared memory support in the settings. Note that the free version of HWINFO64 limits the shared memory functionality to 12 hours of continuous operation.
   - Set the sensor data update frequency to a value less than 900ms for optimal performance.

3. **Run IODThermalGuard**:
   - Double-click the IODThermalGuard executable to run the software.
   - **Administrator Permissions**: The software requires administrator permissions to adjust the GPU clock limits. If not run as an administrator, the adjustments will not take effect.
   - On the first run, a configuration file will be automatically generated. Generally, you shouldn’t need to modify the default settings.

