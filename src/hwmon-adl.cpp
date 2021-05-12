#include "hwmon-adl.h"
extern "C" {
#include "adl.h"
}

bool HWMonADL::isAvailable()
{
  return prepare_adl();
}

HWMonADL::HWMonADL(int devicesNum)
{
  init_adl(devicesNum);
}

bool HWMonADL::setCoreClock(int gpuIdx, int frequency)
{
  return set_engineclock(gpuIdx, frequency) == 0;
}

bool HWMonADL::setMemoryClock(int gpuIdx, int frequency)
{
  return set_memoryclock(gpuIdx, frequency == 0);
}

bool HWMonADL::setPowertune(int gpuIdx, int percent)
{
  return set_powertune(gpuIdx, percent) == 0;
}

bool HWMonADL::setFanSpeed(int gpuIdx, int percent)
{
  return set_fanspeed(gpuIdx, percent) == 0;
}

std::optional<int> HWMonADL::getCoreClock(int gpuIdx)
{
  return gpu_engineclock(gpuIdx);
}

std::optional<int> HWMonADL::getMemoryClock(int gpuIdx)
{
  return gpu_memclock(gpuIdx);
}

std::optional<int> HWMonADL::getPowertune(int gpuIdx)
{
  return gpu_powertune(gpuIdx);
}

std::optional<int> HWMonADL::getFanSpeed(int gpuIdx)
{
  return gpu_fanspeed(gpuIdx);
}

std::optional<int> HWMonADL::getTemperature(int gpuIdx)
{
  return static_cast<int>(gpu_temp(gpuIdx));
}

std::optional<int> HWMonADL::getActivity(int gpuIdx)
{
  return gpu_activity(gpuIdx);
}
