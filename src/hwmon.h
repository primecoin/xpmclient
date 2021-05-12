#ifndef __HWMON_H_
#define __HWMON_H_

#include <optional>

class HWMon {

public:
  virtual bool setCoreClock(int gpuIdx, int frequency) = 0;
  virtual bool setMemoryClock(int gpuIdx, int frequency) = 0;
  virtual bool setPowertune(int gpuIdx, int percent) = 0;
  virtual bool setFanSpeed(int gpuIdx, int percent) = 0;

  virtual std::optional<int> getCoreClock(int gpuIdx) = 0;
  virtual std::optional<int> getMemoryClock(int gpuIdx) = 0;
  virtual std::optional<int> getPowertune(int gpuIdx) = 0;
  virtual std::optional<int> getFanSpeed(int gpuIdx) = 0;
  virtual std::optional<int> getTemperature(int gpuIdx) = 0;
  virtual std::optional<int> getActivity(int gpuIdx) = 0;
};

class HWMonEmpty : public HWMon {
public:
  bool setCoreClock(int gpuIdx, int frequency) { return false; }
  bool setMemoryClock(int gpuIdx, int frequency) { return false; }
  bool setPowertune(int gpuIdx, int percent) { return false; }
  bool setFanSpeed(int gpuIdx, int percent) { return false; }

  std::optional<int> getCoreClock(int gpuIdx) { return std::optional<int>(); }
  std::optional<int> getMemoryClock(int gpuIdx) { return std::optional<int>(); }
  std::optional<int> getPowertune(int gpuIdx) { return std::optional<int>(); }
  std::optional<int> getFanSpeed(int gpuIdx) { return std::optional<int>(); }
  std::optional<int> getTemperature(int gpuIdx) { return std::optional<int>(); }
  std::optional<int> getActivity(int gpuIdx) { return std::optional<int>(); }
};

#endif //__HWMON_H_
