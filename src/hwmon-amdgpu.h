#ifndef __HWMON_AMDGPU_H_
#define __HWMON_AMDGPU_H_

#include "hwmon.h"
#include <string>
#include <vector>

class HWMonAmdGpu : public HWMon {
private:
  struct Device {
    int deviceId;
    std::string tMon;
    std::string pwm1;
    std::string pwm1Enable;
    std::string pmInfo;
    time_t lastUpdateTime;
    std::optional<int> pwm1Max;
    std::optional<int> sclk;
    std::optional<int> mclk;
    std::optional<int> activity;
  };

private:
  std::vector<Device> _gpuMap;

private:
  void updateGPUInfo(Device &device);

public:
  static bool isAvailable();

public:
  HWMonAmdGpu(int devicesNum);

  bool setCoreClock(int gpuIdx, int frequency);
  bool setMemoryClock(int gpuIdx, int frequency);
  bool setPowertune(int gpuIdx, int percent);
  bool setFanSpeed(int gpuIdx, int percent);

  std::optional<int> getCoreClock(int gpuIdx);
  std::optional<int> getMemoryClock(int gpuIdx);
  std::optional<int> getPowertune(int gpuIdx);
  std::optional<int> getFanSpeed(int gpuIdx);
  std::optional<int> getTemperature(int gpuIdx);
  std::optional<int> getActivity(int gpuIdx);
};

#endif //__HWMON_AMDGPU_H_
