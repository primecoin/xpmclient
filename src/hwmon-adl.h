#ifndef __HWMON_ADL_H__
#define __HWMON_ADL_H__

#include "hwmon.h"

class HWMonADL : public HWMon {
private:

public:
  static bool isAvailable();

public:
  HWMonADL(int devicesNum);

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

#endif //__HWMON_ADL_H__
