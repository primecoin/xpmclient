#include "benchmarks.h"

#include "gmpxx.h"

#include "loguru.hpp"
#include <string.h>
#include <time.h>
#include <chrono>
#include <memory>
#if defined(__GXX_EXPERIMENTAL_CXX0X__) && (__cplusplus < 201103L)
#define steady_clock monotonic_clock
#endif  

#include "prime.h"
#include <math.h> 
#include <set>

const unsigned GroupSize = 256;
const unsigned MulOpsNum = 512;
const unsigned WindowSize = 7;

enum AlgorithmIdTy {
  aidSquare = 0,
  aidMultiply,
  aidMontgomerySquare,
  aidMontgomeryMultiply,
  aidMontgomeryRedchalf,
  aidRedcify
};

enum TestTypeTy {
  ttUnitTest = 0,
  ttPerformanceTest
};

const uint32_t binvert_limb_table[128] = {
  0x01, 0xAB, 0xCD, 0xB7, 0x39, 0xA3, 0xC5, 0xEF,
  0xF1, 0x1B, 0x3D, 0xA7, 0x29, 0x13, 0x35, 0xDF,
  0xE1, 0x8B, 0xAD, 0x97, 0x19, 0x83, 0xA5, 0xCF,
  0xD1, 0xFB, 0x1D, 0x87, 0x09, 0xF3, 0x15, 0xBF,
  0xC1, 0x6B, 0x8D, 0x77, 0xF9, 0x63, 0x85, 0xAF,
  0xB1, 0xDB, 0xFD, 0x67, 0xE9, 0xD3, 0xF5, 0x9F,
  0xA1, 0x4B, 0x6D, 0x57, 0xD9, 0x43, 0x65, 0x8F,
  0x91, 0xBB, 0xDD, 0x47, 0xC9, 0xB3, 0xD5, 0x7F,
  0x81, 0x2B, 0x4D, 0x37, 0xB9, 0x23, 0x45, 0x6F,
  0x71, 0x9B, 0xBD, 0x27, 0xA9, 0x93, 0xB5, 0x5F,
  0x61, 0x0B, 0x2D, 0x17, 0x99, 0x03, 0x25, 0x4F,
  0x51, 0x7B, 0x9D, 0x07, 0x89, 0x73, 0x95, 0x3F,
  0x41, 0xEB, 0x0D, 0xF7, 0x79, 0xE3, 0x05, 0x2F,
  0x31, 0x5B, 0x7D, 0xE7, 0x69, 0x53, 0x75, 0x1F,
  0x21, 0xCB, 0xED, 0xD7, 0x59, 0xC3, 0xE5, 0x0F,
  0x11, 0x3B, 0x5D, 0xC7, 0x49, 0x33, 0x55, 0xFF
};

void logPrint(const char *name, uint32_t *ptr, size_t size)
{
  char buffer[1024];
  char N[16];
  buffer[0] = 0;
  for (size_t i = 0; i < size; i++) {
    snprintf(N, sizeof(N), " %08X", ptr[i]);
    strcat(buffer, N);
  }
  LOG_F(ERROR, "%s:%s", name, buffer);
}

uint32_t invert_limb(uint32_t limb)
{
  uint32_t inv = binvert_limb_table[(limb/2) & 0x7F];
  inv = 2*inv - inv*inv*limb;
  inv = 2*inv - inv*inv*limb;
  return -inv;
}

uint64_t invert_limb(uint64_t limb)
{
  uint64_t inv = binvert_limb_table[(limb/2) & 0x7F];
  inv = 2*inv - inv*inv*limb;
  inv = 2*inv - inv*inv*limb;
  inv = 2*inv - inv*inv*limb;
  return -inv;
}

static inline const char *algorithmName(AlgorithmIdTy id)
{
  switch (id) {
    case aidSquare : return "square";
    case aidMultiply : return "multiply";
    case aidMontgomerySquare : return "monsqr";
    case aidMontgomeryMultiply : return "monmul";
    case aidMontgomeryRedchalf : return "redchalf";
    case aidRedcify : return "redcify";
    default : return "unknown";
  }
}

static inline char kernelSuffix(TestTypeTy type)
{
  switch (type) {
    case ttUnitTest : return 'u';
    case ttPerformanceTest : return 'p';
    default : return '_';
  }
}


uint32_t rand32()
{
  uint32_t result = rand();
  result = (result << 16) | rand();
  return result;
}

uint64_t rand64()
{
  uint64_t result = rand();
  result = (result << 16) | rand();
  result = (result << 16) | rand();
  result = (result << 16);
  return result;
} 


bool trialDivisionChainTest(uint32_t *primes,
                            mpz_class &N,
                            bool fSophieGermain,
                            unsigned chainLength,
                            unsigned depth)
{
  N += (fSophieGermain ? -1 : 1);
  for (unsigned i = 0; i < chainLength; i++) {
    for (unsigned divIdx = 0; divIdx < depth; divIdx += 16) { 
      if (mpz_tdiv_ui(N.get_mpz_t(), primes[divIdx]) == 0) {
        LOG_F(ERROR, "Invalid number found; chain position is %u, divisor is %u type is %u", i+1, primes[divIdx], fSophieGermain ? 1 : 2);
        return false;
      }
    }
     
    N <<= 1;
    N += (fSophieGermain ? 1 : -1);
  }
  
  return true;
} 

bool sieveResultsTest(uint32_t *primes,
                      mpz_class &fixedMultiplier,
                      const uint8_t *cunningham1,
                      const uint8_t *cunningham2,
                      unsigned sieveSize,
                      unsigned chainLength,
                      unsigned depth,
                      unsigned extensionsNum,
                      std::set<mpz_class> &candidates,
                      unsigned *invalidCount)
{
  const uint32_t layersNum = chainLength + extensionsNum;
  const uint32_t *c1ptr = (const uint32_t*)cunningham1;  
  const uint32_t *c2ptr = (const uint32_t*)cunningham2;    
  unsigned sieveWords = sieveSize/32;
   
  for (unsigned wordIdx = 0; wordIdx < sieveWords; wordIdx++) {
    uint32_t c1Data[layersNum];
    uint32_t c2Data[layersNum];
     
    for (unsigned i = 0; i < layersNum; i++)
      c1Data[i] = c1ptr[wordIdx + sieveWords*i];
     
    for (unsigned firstLayer = 0; firstLayer <= layersNum-chainLength; firstLayer++) {
      uint32_t mask = 0;
      for (unsigned layer = 0; layer < chainLength; layer++)
        mask |= c1Data[firstLayer + layer];
       
      if (mask != 0xFFFFFFFF) {
        for (unsigned bit = 0; bit < 32; bit++) {
          if ((~mask & (1 << bit))) {
            mpz_class candidateMultiplier = (mpz_class)(sieveSize + wordIdx*32 + bit) << firstLayer;
            mpz_class chainOrigin = fixedMultiplier*candidateMultiplier;
            if (!trialDivisionChainTest(primes, chainOrigin, true, chainLength, depth)) {
              LOG_F(ERROR, " * type 1 firstLayer = %u", firstLayer);
              ++*invalidCount;
            }
            
            candidates.insert(candidateMultiplier);
          }
        }
      }
    }

    for (unsigned i = 0; i < layersNum; i++)
      c2Data[i] = c2ptr[wordIdx + sieveWords*i];
     
    for (unsigned firstLayer = 0; firstLayer <= layersNum-chainLength; firstLayer++) {
      uint32_t mask = 0;
      for (unsigned layer = 0; layer < chainLength; layer++)
        mask |= c2Data[firstLayer + layer];
       
      if (mask != 0xFFFFFFFF) {
        for (unsigned bit = 0; bit < 32; bit++) {
          if ((~mask & (1 << bit))) {
            mpz_class candidateMultiplier = (mpz_class)(sieveSize + wordIdx*32 + bit) << firstLayer;
            mpz_class chainOrigin = fixedMultiplier*candidateMultiplier;
            if (!trialDivisionChainTest(primes, chainOrigin, false, chainLength, depth)) {
              LOG_F(ERROR, " * type 2 firstLayer = %u", firstLayer);
              ++*invalidCount;
            }
            
            candidates.insert(candidateMultiplier);            
          }
        }
      }
    } 
 
    unsigned bitwinLayers = chainLength / 2 + chainLength % 2;
    for (unsigned firstLayer = 0; firstLayer <= layersNum-bitwinLayers; firstLayer++) {
      uint32_t mask = 0;
      for (unsigned layer = 0; layer < chainLength/2; layer++)
        mask |= c1Data[firstLayer + layer] | c2Data[firstLayer + layer];
      if (chainLength & 0x1)      
        mask |= c1Data[firstLayer + chainLength/2];
       
      if (mask != 0xFFFFFFFF) {
        for (unsigned bit = 0; bit < 32; bit++) {
          if ((~mask & (1 << bit))) {
            mpz_class candidateMultiplier = (mpz_class)(sieveSize + wordIdx*32 + bit) << firstLayer;
            mpz_class chainOrigin = fixedMultiplier*candidateMultiplier;
            mpz_class chainOriginExtra = chainOrigin;            
            if (!trialDivisionChainTest(primes, chainOrigin, true, (chainLength+1)/2, depth) ||
                !trialDivisionChainTest(primes, chainOriginExtra, false, chainLength/2, depth)) {
              LOG_F(ERROR, " * type bitwin firstLayer = %u", firstLayer);
              ++*invalidCount;
            }
            candidates.insert(candidateMultiplier);            
          }
        }
      }
    }    
  }
   
  return true;
   
} 

static inline cl_kernel findKernelOrDie(cl_program program, const char *name)
{
  cl_int result;
  cl_kernel kernel = clCreateKernel(program, name, &result);
  if (result != CL_SUCCESS) {
    LOG_F(ERROR, " * Error: can't find kernel %s", name);
    exit(1);
  }

  return kernel;
}

// make Montgomery reduce
void reduce(mp_limb_t *target, mp_limb_t *mod, mp_limb_t *prod, mp_limb_t *add1, mp_limb_t *add2, mp_limb_t invm, unsigned operandSize, unsigned opLimbsNum)
{
  if (!(sizeof(mp_limb_t) == 8 && (operandSize % 2) == 1)) {
    for (unsigned j = 0; j < opLimbsNum; j++)
      prod[j] = mpn_addmul_1(&prod[j], mod, opLimbsNum, prod[j]*invm);
    mp_limb_t highestLimb = mpn_add_n(target, prod, prod+opLimbsNum, opLimbsNum);
    if (highestLimb)
      mpn_sub_n(target, target, mod, opLimbsNum);
  } else {
    uint32_t *prod32 = reinterpret_cast<uint32_t*>(prod);
    uint32_t *add1p32 = reinterpret_cast<uint32_t*>(add1);
    uint32_t *target32 = reinterpret_cast<uint32_t*>(target);
    for (unsigned j = 0; j < opLimbsNum-1; j++)
      prod[j] = mpn_addmul_1(&prod[j], mod, opLimbsNum, prod[j]*invm);

    // make a half mpn_addmul_1 (for highest 32-bit limb)
    prod32[operandSize-1] = (uint32_t)mpn_addmul_1(&prod[opLimbsNum-1], mod, opLimbsNum, (prod[opLimbsNum-1]*invm) & 0xFFFFFFFF);

    // make aligned addends
    memcpy(add1p32 + 1, prod, 4*operandSize);
    memcpy(add2, prod32+operandSize, 4*operandSize);

    mpn_add_n(target, add1, add2, opLimbsNum);
    if (target32[operandSize])
      mpn_sub_n(target, target, mod, opLimbsNum);
  }
}

void multiplyBenchmark(cl_context context,
                       cl_command_queue queue,
                       cl_program program,
                       unsigned groupsNum,                       
                       unsigned operand1Size,
                       unsigned operand2Size,
                       uint32_t elementsNum,
                       AlgorithmIdTy algoId,
                       TestTypeTy testType)
{
  char kernelNameBuffer[256] = "unknown";
  if (algoId == aidSquare)
    snprintf(kernelNameBuffer, sizeof(kernelNameBuffer), "%sBenchmark%u%c", algorithmName(algoId), operand1Size*32, kernelSuffix(testType));
  else if (algoId == aidMultiply)
    snprintf(kernelNameBuffer, sizeof(kernelNameBuffer), "%sBenchmark%uto%u%c", algorithmName(algoId), operand1Size*32, operand2Size*32, kernelSuffix(testType));
  cl_kernel kernel = findKernelOrDie(program, kernelNameBuffer);

  unsigned resultSize = (algoId == aidSquare) ? 2*operand1Size : operand1Size+operand2Size;
  unsigned op1MemSize = operand1Size + (operand1Size%2);
  unsigned op2MemSize = operand2Size + (operand2Size%2);
  unsigned resultMemSize = (algoId == aidSquare) ? 2*op1MemSize : op1MemSize+op2MemSize;

  clBuffer<uint32_t> m1;
  clBuffer<uint32_t> m2;
  clBuffer<uint32_t> mR;
  clBuffer<uint32_t> cpuR;

  {
    unsigned op1LimbsNum = elementsNum*op1MemSize;
    unsigned op2LimbsNum = elementsNum*op2MemSize;
    unsigned resultLimbsNum = elementsNum*resultMemSize;
    OCL(m1.init(context, op1LimbsNum, CL_MEM_READ_WRITE));
    if (operand2Size)
      OCL(m2.init(context, op2LimbsNum, CL_MEM_READ_WRITE));
    OCL(mR.init(context, resultLimbsNum, CL_MEM_READ_WRITE));
    OCL(cpuR.init(context, resultLimbsNum, CL_MEM_READ_WRITE));
    memset(&m1.get(0), 0, op1LimbsNum*sizeof(uint32_t));
    memset(&m2.get(0), 0, op2LimbsNum*sizeof(uint32_t));
    memset(&mR.get(0), 0, resultLimbsNum*sizeof(uint32_t));
    memset(&cpuR.get(0), 0, resultLimbsNum*sizeof(uint32_t));
  }

  for (unsigned i = 0; i < elementsNum; i++) {
    for (unsigned j = 0; j < operand1Size; j++)
      m1[i*op1MemSize + j] = rand32();
    for (unsigned j = 0; j < operand2Size; j++)
      m2[i*op2MemSize + j] = rand32();
  }

  OCL(m1.copyToDevice(queue));
  if (operand2Size)
    OCL(m2.copyToDevice(queue));

  if (algoId == aidSquare) {
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &m1.DeviceData);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &mR.DeviceData);
    clSetKernelArg(kernel, 2, sizeof(elementsNum), &elementsNum);
  } else {
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &m1.DeviceData);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &m2.DeviceData);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &mR.DeviceData);
    clSetKernelArg(kernel, 3, sizeof(elementsNum), &elementsNum);
  }

  clFinish(queue);
  auto gpuBegin = std::chrono::steady_clock::now();  
  
  {
    size_t globalThreads[1] = { groupsNum*GroupSize };
    size_t localThreads[1] = { GroupSize };
    cl_event event;
    cl_int result;
    if ((result = clEnqueueNDRangeKernel(queue,
                                         kernel,
                                         1,
                                         0,
                                         globalThreads,
                                         localThreads,
                                         0, 0, &event)) != CL_SUCCESS) {
      LOG_F(ERROR, "clEnqueueNDRangeKernel error!");
      return;
    }

    if (clWaitForEvents(1, &event) != CL_SUCCESS) {
      LOG_F(ERROR, "clWaitForEvents error!");
      return;
    }
    
    clReleaseEvent(event);
  }
  
  clFinish(queue);
  auto gpuEnd = std::chrono::steady_clock::now();  
  unsigned op1LimbsNum = op1MemSize*sizeof(uint32_t)/sizeof(mp_limb_t);
  unsigned op2LimbsNum = op2MemSize*sizeof(uint32_t)/sizeof(mp_limb_t);
  if (algoId == aidSquare) {
    for (unsigned i = 0; i < elementsNum; i++) {
      mp_limb_t *Operand1 = (mp_limb_t*)&m1[i*op1MemSize]; //cpuM1[i].get_mpz_t()->_mp_d;
      mp_limb_t *target = (mp_limb_t*)&cpuR[i*resultMemSize];
      uint32_t *target32 = reinterpret_cast<uint32_t*>(target);
      for (unsigned j = 0; j < (testType == ttPerformanceTest ? MulOpsNum : 1); j++) {
        mpn_sqr(target, Operand1, op1LimbsNum);
        memcpy(Operand1, target32+operand1Size, op1LimbsNum*sizeof(mp_limb_t));
      }
    }
  } else {
    for (unsigned i = 0; i < elementsNum; i++) {
      mp_limb_t *Operand1 = (mp_limb_t*)&m1[i*op1MemSize];
      mp_limb_t *Operand2 = (mp_limb_t*)&m2[i*op2MemSize];
      mp_limb_t *target = (mp_limb_t*)&cpuR[i*resultMemSize];
      uint32_t *target32 = reinterpret_cast<uint32_t*>(target);
      for (unsigned j = 0; j < (testType == ttPerformanceTest ? MulOpsNum : 1); j++) {
        mpn_mul(target, Operand1, op1LimbsNum, Operand2, op2LimbsNum);
        memcpy(Operand1, target32+operand2Size, op1LimbsNum*sizeof(mp_limb_t));
      }
    }
  }

  OCL(mR.copyToHost(queue));
  clFinish(queue);

  bool testIsOk = true;
  for (unsigned i = 0; i < elementsNum; i++) {
    if (memcmp(&mR[i*resultMemSize], &cpuR[i*resultMemSize], 4*resultSize) != 0) {
      logPrint("operand1", &m1[i*op1MemSize], operand1Size);
      if (algoId == aidMultiply)
        logPrint("operand2", &m2[i*op2MemSize], operand2Size);
      logPrint("cpu", &cpuR[i*resultMemSize], resultSize);
      logPrint("gpu", &mR[i*resultMemSize], resultSize);
      LOG_F(ERROR, "results differ at element %u!", i);
      testIsOk = false;
      break;
    }
  }

  if (testType == ttPerformanceTest) {
    double gpuTime = std::chrono::duration_cast<std::chrono::microseconds>(gpuEnd-gpuBegin).count() / 1000.0;
    double opsNum = ((elementsNum*MulOpsNum) / 1000000.0) / gpuTime * 1000.0;
    LOG_F(INFO, "%s: %.3lfms (%.3lfM ops/sec)", kernelNameBuffer, gpuTime, opsNum);
  } else {
    LOG_F(INFO, "%s: %s", kernelNameBuffer, testIsOk ? "OK" : "FAIL");
  }
}

void monMulBenchmark(cl_context context,
                     cl_command_queue queue,
                     cl_program program,
                     unsigned groupsNum,
                     unsigned operandSize,
                     uint32_t elementsNum,
                     AlgorithmIdTy algoId,
                     TestTypeTy testType)
{
  char kernelNameBuffer[256] = "unknown";
  snprintf(kernelNameBuffer, sizeof(kernelNameBuffer), "%sBenchmark%u%c", algorithmName(algoId), operandSize*32, kernelSuffix(testType));
  cl_kernel kernel = findKernelOrDie(program, kernelNameBuffer);

  unsigned resultSize = operandSize;
  unsigned opMemSize = operandSize + (operandSize%2);
  unsigned resultMemSize = opMemSize;

  clBuffer<uint32_t> m1;
  clBuffer<uint32_t> m2;
  clBuffer<uint32_t> mmod;
  clBuffer<uint32_t> mR;
  clBuffer<uint32_t> cpuR;

  {
    unsigned opLimbsNum = elementsNum*opMemSize;
    unsigned resultLimbsNum = opLimbsNum;
    OCL(m1.init(context, opLimbsNum, CL_MEM_READ_WRITE));
    if (algoId == aidMontgomeryMultiply)
      OCL(m2.init(context, opLimbsNum, CL_MEM_READ_WRITE));
    OCL(mmod.init(context, opLimbsNum, CL_MEM_READ_WRITE));
    OCL(mR.init(context, resultLimbsNum, CL_MEM_READ_WRITE));
    OCL(cpuR.init(context, resultLimbsNum, CL_MEM_READ_WRITE));
    memset(&m1.get(0), 0, opLimbsNum*sizeof(uint32_t));
    if (algoId == aidMontgomeryMultiply)
      memset(&m2.get(0), 0, opLimbsNum*sizeof(uint32_t));
    memset(&mmod.get(0), 0, opLimbsNum*sizeof(uint32_t));
    memset(&mR.get(0), 0, resultLimbsNum*sizeof(uint32_t));
    memset(&cpuR.get(0), 0, resultLimbsNum*sizeof(uint32_t));
  }

  for (unsigned i = 0; i < elementsNum; i++) {
    for (unsigned j = 0; j < operandSize; j++)
      m1[i*opMemSize + j] = rand32();
    if (algoId == aidMontgomeryMultiply) {
      for (unsigned j = 0; j < operandSize; j++)
        m2[i*opMemSize + j] = rand32();
    }
    for (unsigned j = 0; j < operandSize; j++)
      mmod[i*opMemSize + j] = rand32();
    mmod[i*opMemSize + 0] |= 1;
  }

  OCL(m1.copyToDevice(queue));
  if (algoId == aidMontgomeryMultiply)
    OCL(m2.copyToDevice(queue));
  OCL(mmod.copyToDevice(queue));

  if (algoId == aidMontgomerySquare) {
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &m1.DeviceData);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &mmod.DeviceData);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &mR.DeviceData);
    clSetKernelArg(kernel, 3, sizeof(elementsNum), &elementsNum);
  } else if (algoId == aidMontgomeryMultiply) {
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &m1.DeviceData);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &m2.DeviceData);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &mmod.DeviceData);
    clSetKernelArg(kernel, 3, sizeof(cl_mem), &mR.DeviceData);
    clSetKernelArg(kernel, 4, sizeof(elementsNum), &elementsNum);
  } else if (algoId == aidMontgomeryRedchalf) {
    clSetKernelArg(kernel, 0, sizeof(cl_mem), &m1.DeviceData);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &mmod.DeviceData);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &mR.DeviceData);
    clSetKernelArg(kernel, 3, sizeof(elementsNum), &elementsNum);
  }

  clFinish(queue);
  auto gpuBegin = std::chrono::steady_clock::now();

  {
    size_t globalThreads[1] = { groupsNum*GroupSize };
    size_t localThreads[1] = { GroupSize };
    cl_event event;
    cl_int result;
    if ((result = clEnqueueNDRangeKernel(queue,
                                         kernel,
                                         1,
                                         0,
                                         globalThreads,
                                         localThreads,
                                         0, 0, &event)) != CL_SUCCESS) {
      LOG_F(ERROR, "clEnqueueNDRangeKernel error!");
      return;
    }

    if (clWaitForEvents(1, &event) != CL_SUCCESS) {
      LOG_F(ERROR, "clWaitForEvents error!");
      return;
    }

    clReleaseEvent(event);
  }

  auto gpuEnd = std::chrono::steady_clock::now();

  unsigned opLimbsNum = opMemSize*sizeof(uint32_t)/sizeof(mp_limb_t);
  std::unique_ptr<mp_limb_t[]> prod(new mp_limb_t[2*opLimbsNum]);
  std::unique_ptr<mp_limb_t[]> add1(new mp_limb_t[2*opLimbsNum]);
  std::unique_ptr<mp_limb_t[]> add2(new mp_limb_t[2*opLimbsNum]);

  if (algoId == aidMontgomerySquare) {
    for (unsigned i = 0; i < elementsNum; i++) {
      mp_limb_t *op = (mp_limb_t*)&m1[i*opMemSize];
      mp_limb_t *mod = (mp_limb_t*)&mmod[i*opMemSize];
      mp_limb_t *target = (mp_limb_t*)&cpuR[i*resultMemSize];

      memset(prod.get(), 0, sizeof(mp_limb_t)*2*opLimbsNum);
      memset(add1.get(), 0, sizeof(mp_limb_t)*2*opLimbsNum);
      memset(add2.get(), 0, sizeof(mp_limb_t)*2*opLimbsNum);

      // perform squaring
      mpn_sqr(prod.get(), op, opLimbsNum);

      // calculate inverted limb for mmod[0]
      mp_limb_t invm = invert_limb(mod[0]);

      // make Montgomery reduce
      reduce(target, mod, prod.get(), add1.get(), add2.get(), invm, operandSize, opLimbsNum);
    }
  } else if (algoId == aidMontgomeryMultiply) {
    for (unsigned i = 0; i < elementsNum; i++) {
      mp_limb_t *op1 = (mp_limb_t*)&m1[i*opMemSize];
      mp_limb_t *op2 = (mp_limb_t*)&m2[i*opMemSize];
      mp_limb_t *mod = (mp_limb_t*)&mmod[i*opMemSize];
      mp_limb_t *target = (mp_limb_t*)&cpuR[i*resultMemSize];

      memset(prod.get(), 0, sizeof(mp_limb_t)*2*opLimbsNum);
      memset(add1.get(), 0, sizeof(mp_limb_t)*2*opLimbsNum);
      memset(add2.get(), 0, sizeof(mp_limb_t)*2*opLimbsNum);

      // perform multiplication
      mpn_mul_n(prod.get(), op1, op2, opLimbsNum);

      // calculate inverted limb for mmod[0]
      mp_limb_t invm = invert_limb(mod[0]);

      // make Montgomery reduce
      reduce(target, mod, prod.get(), add1.get(), add2.get(), invm, operandSize, opLimbsNum);
    }
  } else if (algoId == aidMontgomeryRedchalf) {
    for (unsigned i = 0; i < elementsNum; i++) {
      mp_limb_t *op = (mp_limb_t*)&m1[i*opMemSize];
      mp_limb_t *mod = (mp_limb_t*)&mmod[i*opMemSize];
      mp_limb_t *target = (mp_limb_t*)&cpuR[i*resultMemSize];

      memset(prod.get(), 0, sizeof(mp_limb_t)*2*opLimbsNum);
      memset(add1.get(), 0, sizeof(mp_limb_t)*2*opLimbsNum);
      memset(add2.get(), 0, sizeof(mp_limb_t)*2*opLimbsNum);

      memcpy(prod.get(), op, sizeof(mp_limb_t)*opLimbsNum);

      // calculate inverted limb for mmod[0]
      mp_limb_t invm = invert_limb(mod[0]);

      // make Montgomery reduce
      reduce(target, mod, prod.get(), add1.get(), add2.get(), invm, operandSize, opLimbsNum);
    }
  }

  OCL(mR.copyToHost(queue));
  clFinish(queue);

  bool testIsOk = true;
  for (unsigned i = 0; i < elementsNum; i++) {
    if (memcmp(&mR[i*resultMemSize], &cpuR[i*resultMemSize], 4*resultSize) != 0) {
      logPrint("operand1", &m1[i*opMemSize], operandSize);
      if (algoId == aidMontgomeryMultiply)
        logPrint("operand2", &m2[i*opMemSize], operandSize);
      logPrint("modulo", &mmod[i*opMemSize], operandSize);
      logPrint("cpu", &cpuR[i*resultMemSize], resultSize);
      logPrint("gpu", &mR[i*resultMemSize], resultSize);
      LOG_F(ERROR, "results differ at index %u!", i);
      testIsOk = false;
      break;
    }
  }

  if (testType == ttPerformanceTest) {
    double gpuTime = std::chrono::duration_cast<std::chrono::microseconds>(gpuEnd-gpuBegin).count() / 1000.0;
    double opsNum = ((elementsNum*MulOpsNum) / 1000000.0) / gpuTime * 1000.0;
    LOG_F(INFO, "%s %u bits: %.3lfms (%.3lfM ops/sec)", kernelNameBuffer, operandSize*32, gpuTime, opsNum);
  } else {
    LOG_F(INFO, "%s bits: %s", kernelNameBuffer, testIsOk ? "OK" : "FAIL");
  }
}

void redcifyBenchmark(cl_context context,
                      cl_command_queue queue,
                      cl_program program,
                      unsigned groupsNum,
                      unsigned operandSize,
                      uint32_t elementsNum,
                      AlgorithmIdTy algoId,
                      TestTypeTy testType)
{
  char kernelNameBuffer[256] = "unknown";
  snprintf(kernelNameBuffer, sizeof(kernelNameBuffer), "%sBenchmark%u%c", algorithmName(algoId), operandSize*32, kernelSuffix(testType));
  cl_kernel kernel = findKernelOrDie(program, kernelNameBuffer);

  unsigned resultSize = operandSize;
  unsigned opMemSize = operandSize + (operandSize%2);
  unsigned resultMemSize = opMemSize;

  clBuffer<uint32_t> mquotient;
  clBuffer<uint32_t> mmod;
  clBuffer<uint32_t> mR;
  clBuffer<uint32_t> cpuR;

  {
    OCL(mquotient.init(context, elementsNum*256/32, CL_MEM_READ_WRITE));
    OCL(mmod.init(context, elementsNum*opMemSize, CL_MEM_READ_WRITE));
    OCL(mR.init(context, elementsNum*resultMemSize, CL_MEM_READ_WRITE));
    OCL(cpuR.init(context, elementsNum*resultMemSize, CL_MEM_READ_WRITE));
    memset(&mquotient.get(0), 0, sizeof(uint32_t)*mquotient.Size);
    memset(&mmod.get(0), 0, sizeof(uint32_t)*mmod.Size);
    memset(&mR.get(0), 0, sizeof(uint32_t)*mR.Size);
    memset(&cpuR.get(0), 0, sizeof(uint32_t)*cpuR.Size);
  }

  for (unsigned i = 0; i < elementsNum; i++) {
    for (unsigned j = 0; j < 8; j++)
      mquotient[i*8 + j] = rand32();
    for (unsigned j = 0; j < operandSize; j++)
      mmod[i*opMemSize + j] = rand32();
    mmod[i*opMemSize + 0] |= 1;
  }

  OCL(mquotient.copyToDevice(queue));
  OCL(mmod.copyToDevice(queue));

  clSetKernelArg(kernel, 0, sizeof(cl_mem), &mquotient.DeviceData);
  clSetKernelArg(kernel, 1, sizeof(cl_mem), &mmod.DeviceData);
  clSetKernelArg(kernel, 2, sizeof(cl_mem), &mR.DeviceData);
  clSetKernelArg(kernel, 3, sizeof(elementsNum), &elementsNum);

  clFinish(queue);
  auto gpuBegin = std::chrono::steady_clock::now();

  {
    size_t globalThreads[1] = { groupsNum*GroupSize };
    size_t localThreads[1] = { GroupSize };
    cl_event event;
    cl_int result;
    if ((result = clEnqueueNDRangeKernel(queue,
                                         kernel,
                                         1,
                                         0,
                                         globalThreads,
                                         localThreads,
                                         0, 0, &event)) != CL_SUCCESS) {
      LOG_F(ERROR, "clEnqueueNDRangeKernel error!");
      return;
    }

    if (clWaitForEvents(1, &event) != CL_SUCCESS) {
      LOG_F(ERROR, "clWaitForEvents error!");
      return;
    }

    clReleaseEvent(event);
  }

  auto gpuEnd = std::chrono::steady_clock::now();

  unsigned opLimbsNum = opMemSize*sizeof(uint32_t)/sizeof(mp_limb_t);
  constexpr unsigned quotientLimbsNum = (256/8)/sizeof(mp_limb_t);
  std::unique_ptr<mp_limb_t[]> product(new mp_limb_t[quotientLimbsNum + opLimbsNum]);
  mp_limb_t newq[quotientLimbsNum];

  for (unsigned i = 0; i < elementsNum; i++) {
    mp_limb_t *quotient = (mp_limb_t*)&mquotient[i*8];
    mp_limb_t *mod = (mp_limb_t*)&mmod[i*opMemSize];
    mp_limb_t *target = (mp_limb_t*)&cpuR[i*resultMemSize];

    memset(newq, 0, sizeof(newq));

    unsigned N = i % (1 << WindowSize);
    unsigned shiftCount = (1 << WindowSize) - N;
    unsigned limbOffset = shiftCount / (8*sizeof(mp_limb_t));

    memcpy(newq, quotient+limbOffset, sizeof(mp_limb_t)*(quotientLimbsNum - limbOffset));
    if (shiftCount % (8*sizeof(mp_limb_t)))
      mpn_rshift(newq, newq, quotientLimbsNum, shiftCount % (8*sizeof(mp_limb_t)));

    unsigned multiplierSizeInBytes = (64 + (1 << WindowSize)) / 8;
    memset(((uint8_t*)newq) + multiplierSizeInBytes, 0, 256/8 - multiplierSizeInBytes);


    mpn_mul(product.get(), mod, opLimbsNum, newq, quotientLimbsNum);
    for (unsigned i = 0; i < opLimbsNum; i++)
      target[i] = ~product[i];
    target[0]++;
  }

  OCL(mR.copyToHost(queue));
  clFinish(queue);

  bool testIsOk = true;
  for (unsigned i = 0; i < elementsNum; i++) {
    if (memcmp(&mR[i*resultMemSize], &cpuR[i*resultMemSize], 4*resultSize) != 0) {
      LOG_F(ERROR, "element index: %u", i);
      LOG_F(ERROR, "gmp: ");
      for (unsigned j = 0; j < resultSize; j++)
        LOG_F(ERROR, "%08X ", cpuR[i*resultMemSize + j]);
      LOG_F(ERROR, "gpu: ");
      for (unsigned j = 0; j < resultSize; j++)
        LOG_F(ERROR, "%08X ", mR[i*resultMemSize + j]);
      LOG_F(ERROR, " ");
      LOG_F(ERROR, "results differ!");
      testIsOk = false;
      break;
    }
  }

  if (testType == ttPerformanceTest) {
    double gpuTime = std::chrono::duration_cast<std::chrono::microseconds>(gpuEnd-gpuBegin).count() / 1000.0;
    double opsNum = ((elementsNum*MulOpsNum) / 1000000.0) / gpuTime * 1000.0;
    LOG_F(INFO, "%s: %.3lfms (%.3lfM ops/sec)", kernelNameBuffer, gpuTime, opsNum);
  } else {
    LOG_F(INFO, "%s bits: %s", kernelNameBuffer, testIsOk ? "OK" : "FAIL");
  }
}

void divideBenchmark(cl_context context,
                     cl_command_queue queue,
                     cl_program program,
                     unsigned groupsNum,
                     unsigned divisorSize,
                     uint32_t elementsNum,
                     TestTypeTy testType)
{
    char kernelNameBuffer[256] = "unknown";
    snprintf(kernelNameBuffer, sizeof(kernelNameBuffer), "divideBenchmark%uto%u%c", divisorSize*32+160, divisorSize*32, kernelSuffix(testType));
    cl_kernel kernel = findKernelOrDie(program, kernelNameBuffer);

    unsigned opMemSize = divisorSize + (divisorSize%2);
    unsigned quotientSize = 256/32;

    clBuffer<uint32_t> mdivisor;
    clBuffer<uint32_t> mquotient;
    clBuffer<uint32_t> mdivisorBits;
    std::unique_ptr<uint32_t[]> mcpuQuotient(new uint32_t[elementsNum*quotientSize]);
    std::unique_ptr<uint32_t[]> mcpuDivisorBits(new uint32_t[elementsNum]);

    OCL(mdivisor.init(context, elementsNum*opMemSize, CL_MEM_READ_WRITE));
    OCL(mquotient.init(context, elementsNum*quotientSize, CL_MEM_READ_WRITE));
    OCL(mdivisorBits.init(context, elementsNum*4, CL_MEM_READ_WRITE));

    memset(&mdivisor.get(0), 0, sizeof(uint32_t)*elementsNum*opMemSize);
    memset(&mquotient.get(0), 0, sizeof(uint32_t)*elementsNum*quotientSize);
    memset(&mdivisorBits.get(0), 0, sizeof(uint32_t)*elementsNum);
    memset(mcpuQuotient.get(), 0, sizeof(uint32_t)*elementsNum*quotientSize);
    memset(mcpuDivisorBits.get(), 0, sizeof(uint32_t)*elementsNum);

    for (unsigned i = 0; i < elementsNum; i++) {
      for (unsigned j = 0; j < (divisorSize - rand()%4); j++)
        mdivisor[i*opMemSize + j] = rand32();
    }

    OCL(mdivisor.copyToDevice(queue));

    clSetKernelArg(kernel, 0, sizeof(cl_mem), &mdivisor.DeviceData);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &mquotient.DeviceData);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &mdivisorBits.DeviceData);
    clSetKernelArg(kernel, 3, sizeof(elementsNum), &elementsNum);

    clFinish(queue);
    auto gpuBegin = std::chrono::steady_clock::now();

    {
      size_t globalThreads[1] = { groupsNum*GroupSize };
      size_t localThreads[1] = { GroupSize };
      cl_event event;
      cl_int result;
      if ((result = clEnqueueNDRangeKernel(queue,
                                           kernel,
                                           1,
                                           0,
                                           globalThreads,
                                           localThreads,
                                           0, 0, &event)) != CL_SUCCESS) {
        LOG_F(ERROR, "clEnqueueNDRangeKernel error!");
        return;
      }

      if (clWaitForEvents(1, &event) != CL_SUCCESS) {
        LOG_F(ERROR, "clWaitForEvents error!");
        return;
      }

      clReleaseEvent(event);
    }

    auto gpuEnd = std::chrono::steady_clock::now();

    unsigned dividendSize = divisorSize+5;
    unsigned dividendLimbs = dividendSize + (dividendSize % 2);
    unsigned maxDivisorLimbs = opMemSize*4/sizeof(mp_limb_t);
    std::unique_ptr<mp_limb_t[]> quotientBuffer(new mp_limb_t[dividendLimbs]);
    std::unique_ptr<mp_limb_t[]> dividend(new mp_limb_t[dividendLimbs]);
    std::unique_ptr<mp_limb_t[]> remainder(new mp_limb_t[dividendLimbs]);
    uint32_t *dividendp32 = reinterpret_cast<uint32_t*>(dividend.get());

    // CPU check
    for (unsigned i = 0; i < elementsNum; i++) {
      mp_limb_t *divisor = (mp_limb_t*)&mdivisor[i*opMemSize];
      mp_limb_t *target = (mp_limb_t*)&mcpuQuotient[i*quotientSize];
      unsigned divisorLimbs = 0;
      for (unsigned j = 0; j < maxDivisorLimbs; j++) {
        if (divisor[j])
          divisorLimbs++;
        else
          break;
      }

      memset(quotientBuffer.get(), 0, sizeof(mp_limb_t)*dividendLimbs);
      memset(dividend.get(), 0, sizeof(mp_limb_t)*dividendLimbs);
      dividendp32[dividendSize-1] = 0x1;
      mpn_tdiv_qr(quotientBuffer.get(), remainder.get(), 0, dividend.get(), dividendLimbs, divisor, divisorLimbs);
      memcpy(target, quotientBuffer.get(), quotientSize*4);

      mcpuDivisorBits[i] = mpn_sizeinbase(divisor, divisorLimbs, 2);
    }

    OCL(mquotient.copyToHost(queue));
    OCL(mdivisorBits.copyToHost(queue));
    clFinish(queue);

    bool testIsOk = true;
    for (unsigned i = 0; i < elementsNum; i++) {
      if (memcmp(&mquotient[i*quotientSize], &mcpuQuotient[i*quotientSize], 4*quotientSize) != 0 ||
          mdivisorBits[i] != mcpuDivisorBits[i]) {
        logPrint("divisor", &mdivisor[i*opMemSize], divisorSize);
        logPrint("cpu", &mcpuQuotient[i*quotientSize], 8);
        logPrint("gpu", &mquotient[i*quotientSize], 8);
        LOG_F(ERROR, "cpu size: %u", mcpuDivisorBits[i]);
        LOG_F(ERROR, "gpu size: %u", mdivisorBits[i]);
        LOG_F(ERROR, "results differ at element %u!", i);
        testIsOk = false;
        break;
      }
    }

    if (testType == ttPerformanceTest) {
      double gpuTime = std::chrono::duration_cast<std::chrono::microseconds>(gpuEnd-gpuBegin).count() / 1000.0;
      double opsNum = ((elementsNum*MulOpsNum) / 1000000.0) / gpuTime * 1000.0;
      LOG_F(INFO, "%s %u bits: %.3lfms (%.3lfM ops/sec)", kernelNameBuffer, divisorSize*32, gpuTime, opsNum);
    } else {
      LOG_F(INFO, "%s bits: %s", kernelNameBuffer, testIsOk ? "OK" : "FAIL");
    }
}

void fermatTestBenchmark(cl_context context,
                         cl_command_queue queue,
                         cl_program program,
                         unsigned groupsNum, 
                         unsigned operandSize,
                         unsigned elementsNum)
{ 
  char kernelNameBuffer[256];
  snprintf(kernelNameBuffer, sizeof(kernelNameBuffer), "%sBenchmark%u", "fermatTest", operandSize*32);
  cl_kernel kernel = findKernelOrDie(program, kernelNameBuffer);

  unsigned opMemSize = operandSize + (operandSize%2);
  unsigned numberLimbsNum = elementsNum*opMemSize;

  
  clBuffer<uint32_t> numbers;
  clBuffer<uint32_t> gpuResults;
  clBuffer<uint32_t> cpuResults;
  
  OCL(numbers.init(context, numberLimbsNum, CL_MEM_READ_WRITE));
  OCL(gpuResults.init(context, numberLimbsNum, CL_MEM_READ_WRITE));
  OCL(cpuResults.init(context, numberLimbsNum, CL_MEM_READ_WRITE));
  
  for (unsigned i = 0; i < elementsNum; i++) {
    for (unsigned j = 0; j < operandSize; j++)
      numbers[i*opMemSize + j] = (j == operandSize-1) ? (1 << (i % 32)) : rand32();
    if (rand() % 16 == 0) {
      numbers[i*opMemSize + operandSize-2] = numbers[i*opMemSize + operandSize-1];
      numbers[i*opMemSize + operandSize-1] = 0;
    }
    numbers[i*opMemSize] |= 0x1;
  }

  OCL(numbers.copyToDevice(queue));
  OCL(gpuResults.copyToDevice(queue));

  clSetKernelArg(kernel, 0, sizeof(cl_mem), &numbers.DeviceData);
  clSetKernelArg(kernel, 1, sizeof(cl_mem), &gpuResults.DeviceData);
  clSetKernelArg(kernel, 2, sizeof(elementsNum), &elementsNum);
  
  std::unique_ptr<mpz_t[]> cpuNumbersBuffer(new mpz_t[elementsNum]);
  std::unique_ptr<mpz_t[]> cpuResultsBuffer(new mpz_t[elementsNum]);
  mpz_class mpzTwo = 2;
  mpz_class mpzE;
  mpz_import(mpzE.get_mpz_t(), operandSize, -1, 4, 0, 0, &numbers[0]);
  for (unsigned i = 0; i < elementsNum; i++) {
    mpz_init(cpuNumbersBuffer[i]);
    mpz_init(cpuResultsBuffer[i]);
    mpz_import(cpuNumbersBuffer[i], operandSize, -1, 4, 0, 0, &numbers[i*opMemSize]);
    mpz_import(cpuResultsBuffer[i], operandSize, -1, 4, 0, 0, &cpuResults[i*operandSize]);
  }
  
  clFinish(queue);
  auto gpuBegin = std::chrono::steady_clock::now();  

  {
    size_t globalThreads[1] = { groupsNum*GroupSize };
    size_t localThreads[1] = { GroupSize };
    cl_event event;
    cl_int result;
    if ((result = clEnqueueNDRangeKernel(queue,
                                         kernel,
                                         1,
                                         0,
                                         globalThreads,
                                         localThreads,
                                         0, 0, &event)) != CL_SUCCESS) {
      LOG_F(ERROR, "clEnqueueNDRangeKernel error!");
      return;
    }
      
    cl_int error;
    if ((error = clWaitForEvents(1, &event)) != CL_SUCCESS) {
      LOG_F(ERROR, "clWaitForEvents error %i!", error);
      return;
    }
      
    clReleaseEvent(event);
  }
  
  auto gpuEnd = std::chrono::steady_clock::now();  
  
  
  for (unsigned i = 0; i < elementsNum; i++) {
    mpz_sub_ui(mpzE.get_mpz_t(), cpuNumbersBuffer[i], 1);
    mpz_powm(cpuResultsBuffer[i], mpzTwo.get_mpz_t(), mpzE.get_mpz_t(), cpuNumbersBuffer[i]);
  }

  auto cpuEnd = std::chrono::steady_clock::now();     
  
  OCL(gpuResults.copyToHost(queue));
  clFinish(queue);
  
  memset(&cpuResults[0], 0, 4*operandSize*elementsNum);
  for (unsigned i = 0; i < elementsNum; i++) {
    size_t exportedLimbs;
    mpz_export(&cpuResults[i*operandSize], &exportedLimbs, -1, 4, 0, 0, cpuResultsBuffer[i]);
    if (memcmp(&gpuResults[i*opMemSize], &cpuResults[i*operandSize], 4*operandSize) != 0) {
      LOG_F(ERROR, "element index: %u", i);
      LOG_F(ERROR, "element data:");
      for (unsigned j = 0; j < operandSize; j++)
        LOG_F(ERROR, "%08X ", numbers[i*operandSize + j]);
      LOG_F(ERROR, "gmp: ");
      for (unsigned j = 0; j < operandSize; j++)
        LOG_F(ERROR, "%08X ", cpuResults[i*operandSize + j]);
      LOG_F(ERROR, "gpu: ");
      for (unsigned j = 0; j < operandSize; j++)
        LOG_F(ERROR, "%08X ", gpuResults[i*opMemSize + j]);
      LOG_F(ERROR, "results differ!");
      break;
    }
  }
  
  double gpuTime = std::chrono::duration_cast<std::chrono::microseconds>(gpuEnd-gpuBegin).count() / 1000.0;  
  double cpuTime = std::chrono::duration_cast<std::chrono::microseconds>(cpuEnd-gpuEnd).count() / 1000.0;    
  double opsNum = ((elementsNum) / 1000000.0) / gpuTime * 1000.0;
  double cpuOpsNum = ((elementsNum) / 1000000.0) / cpuTime * 1000.0;
  
  LOG_F(INFO, "%s %u bits: %.3lfms (%.3lfM ops/sec, single thread cpu: %.3lfM ops/sec)", "Fermat tests", operandSize*32, gpuTime, opsNum, cpuOpsNum);
}


void hashmodBenchmark(cl_context context,
                      cl_command_queue queue,
                      cl_program program,
                      unsigned defaultGroupSize,
                      unsigned,
                      mpz_class *allPrimorials,
                      unsigned mPrimorial)
{
  LOG_F(INFO, "*** hashmod benchmark ***");
  
  const unsigned iterationsNum = 64;
  cl_kernel mHashMod = findKernelOrDie(program, "bhashmodUsePrecalc");

  PrimeMiner::search_t hashmod;
  PrimeMiner::block_t blockheader;
  
  OCL(hashmod.midstate.init(context, 8*sizeof(cl_uint), CL_MEM_READ_ONLY));
  OCL(hashmod.found.init(context, 32768, CL_MEM_READ_WRITE));
  OCL(hashmod.primorialBitField.init(context, 2048, CL_MEM_READ_WRITE));
  OCL(hashmod.count.init(context, 1, CL_MEM_READ_WRITE));

  clSetKernelArg(mHashMod, 0, sizeof(cl_mem), &hashmod.found.DeviceData);
  clSetKernelArg(mHashMod, 1, sizeof(cl_mem), &hashmod.count.DeviceData);
  clSetKernelArg(mHashMod, 2, sizeof(cl_mem), &hashmod.primorialBitField.DeviceData);
  clSetKernelArg(mHashMod, 3, sizeof(cl_mem), &hashmod.midstate.DeviceData);

  uint64_t totalTime = 0;
  unsigned totalHashes = 0;
  unsigned numhash = 64 * 131072;

  unsigned multiplierSizes[128];
  memset(multiplierSizes, 0, sizeof(multiplierSizes));
  
  uint64_t phashCount[20];
  memset(phashCount, 0, sizeof(phashCount));
  
  for (unsigned i = 0; i < iterationsNum; i++) {
    {
      sha256precalcData data;
      uint8_t *pHeader = (uint8_t*)&blockheader;
      for (unsigned i = 0; i < sizeof(blockheader); i++)
        pHeader[i] = rand32();
      blockheader.version = PrimeMiner::block_t::CURRENT_VERSION;
      blockheader.nonce = 1;  

      precalcSHA256(&blockheader, hashmod.midstate.HostData, &data);
      OCL(hashmod.midstate.copyToDevice(queue));
      OCL(clSetKernelArg(mHashMod, 4, sizeof(cl_uint), &data.merkle));
      OCL(clSetKernelArg(mHashMod, 5, sizeof(cl_uint), &data.time));
      OCL(clSetKernelArg(mHashMod, 6, sizeof(cl_uint), &data.nbits));
      OCL(clSetKernelArg(mHashMod, 7, sizeof(cl_uint), &data.W0));
      OCL(clSetKernelArg(mHashMod, 8, sizeof(cl_uint), &data.W1));
      OCL(clSetKernelArg(mHashMod, 9, sizeof(cl_uint), &data.new1_0));
      OCL(clSetKernelArg(mHashMod, 10, sizeof(cl_uint), &data.new1_1));
      OCL(clSetKernelArg(mHashMod, 11, sizeof(cl_uint), &data.new1_2));
      OCL(clSetKernelArg(mHashMod, 12, sizeof(cl_uint), &data.new2_0));
      OCL(clSetKernelArg(mHashMod, 13, sizeof(cl_uint), &data.new2_1));
      OCL(clSetKernelArg(mHashMod, 14, sizeof(cl_uint), &data.new2_2));
      OCL(clSetKernelArg(mHashMod, 15, sizeof(cl_uint), &data.temp2_3));
    }    

    OCL(hashmod.count.copyToDevice(queue, false));
    
    size_t globalSize[] = { numhash, 1u, 1u };
    size_t localSize[] = { defaultGroupSize, 1 };
 
    hashmod.count[0] = 0;
    OCL(hashmod.count.copyToDevice(queue));
    clFinish(queue);
    auto gpuBegin = std::chrono::steady_clock::now();  
    
    {
      cl_event event;
      cl_int result;
      if ((result = clEnqueueNDRangeKernel(queue,
                                           mHashMod,
                                           1,
                                           0,
                                           globalSize,
                                           localSize,
                                           0,
                                           0, &event)) != CL_SUCCESS) {
        LOG_F(ERROR, "clEnqueueNDRangeKernel error!");
        return;
      }
        
      cl_int error;
      if ((error = clWaitForEvents(1, &event)) != CL_SUCCESS) {
        LOG_F(ERROR, "clWaitForEvents error %i!", error);
        return;
      }
        
      clReleaseEvent(event);
    } 
    
    auto gpuEnd = std::chrono::steady_clock::now();  
    
    OCL(hashmod.found.copyToHost(queue, false));
    OCL(hashmod.primorialBitField.copyToHost(queue, false));
    OCL(hashmod.count.copyToHost(queue, false));
    clFinish(queue);
    
    totalTime += std::chrono::duration_cast<std::chrono::microseconds>(gpuEnd-gpuBegin).count();
    totalHashes += hashmod.count[0];
    
    for (unsigned i = 0; i < hashmod.count[0]; i++) {
      uint256 hashValue;
      PrimeMiner::block_t b = blockheader;
      b.nonce = hashmod.found[i];      
      
      uint32_t primorialBitField = hashmod.primorialBitField[i];
      uint32_t primorialIdx = primorialBitField >> 16;
      uint64_t realPrimorial = 1;
      for (unsigned j = 0; j < primorialIdx+1; j++) {
        if (primorialBitField & (1 << j))
          realPrimorial *= gPrimes[j];
      }      
      
      phashCount[primorialIdx]++;
      
      mpz_class mpzRealPrimorial;        
      mpz_import(mpzRealPrimorial.get_mpz_t(), 2, -1, 4, 0, 0, &realPrimorial);            
      primorialIdx = std::max(mPrimorial, primorialIdx) - mPrimorial;
      mpz_class mpzHashMultiplier = allPrimorials[primorialIdx] / mpzRealPrimorial;
      unsigned hashMultiplierSize = mpz_sizeinbase(mpzHashMultiplier.get_mpz_t(), 2);      
      multiplierSizes[hashMultiplierSize]++;
      
      SHA_256 sha;
      sha.init();
      sha.update((const unsigned char*)&b, sizeof(b));
      sha.final((unsigned char*)&hashValue);
      sha.init();
      sha.update((const unsigned char*)&hashValue, sizeof(uint256));
      sha.final((unsigned char*)&hashValue);      
      
      if(hashValue < (uint256(1) << 255)){
        LOG_F(ERROR, "hash does not meet minimum");
        continue;
      }
      
        
      mpz_class mpzHash;
      mpz_set_uint256(mpzHash.get_mpz_t(), hashValue);
      if(!mpz_divisible_p(mpzHash.get_mpz_t(), mpzRealPrimorial.get_mpz_t())){
        LOG_F(ERROR, "mpz_divisible_ui_p failed");
        continue;
      }    
      
      
      uint32_t multiplierBitField = hashmod.primorialBitField[i];
      
      unsigned multiplierCount = 0;
      for (unsigned j = 0; j < 32; j++)
        multiplierCount += ((multiplierBitField & (1 << j)) != 0);
    }
  }
  
  double averageHashes = (double)totalHashes / iterationsNum;
  LOG_F(INFO, " MHash per second: %.3lf", iterationsNum*numhash / (double)totalTime);
  LOG_F(INFO, " Hash per iteration: %.3lf (%.6lf %%)", averageHashes, averageHashes*100/numhash);
 
  uint64_t totalSize = 0;
  unsigned hashes = 0;
  for (unsigned i = 0; i < 128; i++) {
    if (multiplierSizes[i]) {
      hashes += multiplierSizes[i];
      totalSize += multiplierSizes[i] * i;
    }
  }
  LOG_F(INFO, " Average hash multiplier size: %.3lf", totalSize / (double)hashes);
  
  for (unsigned i = 0; i < 20; i++) {
    if (phashCount[i]) {
      LOG_F(INFO, "   Hashed with primorial %u is %.3lf%%", i, phashCount[i] / (double)hashes * 100.0);
    }
  }
}

void sieveTestBenchmark(cl_context context,
                        cl_command_queue queue,
                        openclPrograms &programs,
                        unsigned defaultGroupSize,
                        unsigned,
                        mpz_class *allPrimorial,
                        unsigned mPrimorial,
                        config_t mConfig,
                        unsigned mDepth,
                        bool checkCandidates)
{
  LOG_F(INFO, "*** sieve (%s) benchmark ***", checkCandidates ? "check" : "performance");
  
  cl_kernel mHashMod = findKernelOrDie(programs.sha256, "bhashmodUsePrecalc");
  cl_kernel mSieveSetup = findKernelOrDie(programs.sieveUtils, "setup_sieve");
  cl_kernel mSieve = findKernelOrDie(programs.sieve, "sieve");
  cl_kernel mSieveSearch = findKernelOrDie(programs.sieveUtils, "s_sieve");

  PrimeMiner::search_t hashmod;
  PrimeMiner::block_t blockheader;
  lifoBuffer<PrimeMiner::hash_t> hashes(PW);
  clBuffer<cl_uint> hashBuf;
  clBuffer<cl_uint> sieveBuf[2];
  clBuffer<cl_uint> sieveOff[2];  
  clBuffer<PrimeMiner::fermat_t> sieveBuffers[64][FERMAT_PIPELINES];
  clBuffer<cl_uint> candidatesCountBuffers[64];

  cl_mem primeBuf[maxHashPrimorial];
  cl_mem primeBuf2[maxHashPrimorial];
  
  for (unsigned i = 0; i < maxHashPrimorial - mPrimorial; i++) {
    cl_int error = 0;
    primeBuf[i] = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                 mConfig.PCOUNT*sizeof(cl_uint), &gPrimes[mPrimorial+i+1], &error);
    OCL(error);
    primeBuf2[i] = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                  mConfig.PCOUNT*2*sizeof(cl_uint), &gPrimes2[2*(mPrimorial+i)+2], &error);
    OCL(error);
  }
  
  clBuffer<cl_uint> modulosBuf[maxHashPrimorial];
  unsigned modulosBufferSize = mConfig.PCOUNT*(mConfig.N-1);   
  for (unsigned bufIdx = 0; bufIdx < maxHashPrimorial-mPrimorial; bufIdx++) {
    clBuffer<cl_uint> &current = modulosBuf[bufIdx];
    OCL(current.init(context, modulosBufferSize, CL_MEM_READ_ONLY));
    for (unsigned i = 0; i < mConfig.PCOUNT; i++) {
      mpz_class X = 1;
      for (unsigned j = 0; j < mConfig.N-1; j++) {
        X <<= 32;
        mpz_class mod = X % gPrimes[i+mPrimorial+bufIdx+1];
        current[mConfig.PCOUNT*j+i] = mod.get_ui();
      }
    }
    
    OCL(current.copyToDevice(queue));
  }  
  
  OCL(hashmod.midstate.init(context, 8*sizeof(cl_uint), CL_MEM_READ_ONLY));
  OCL(hashmod.found.init(context, 2048, CL_MEM_READ_WRITE));
  OCL(hashmod.primorialBitField.init(context, 2048, CL_MEM_READ_WRITE));
  OCL(hashmod.count.init(context, 1, CL_MEM_READ_WRITE));
  OCL(hashBuf.init(context, PW*mConfig.N, CL_MEM_READ_WRITE));

  clSetKernelArg(mHashMod, 0, sizeof(cl_mem), &hashmod.found.DeviceData);
  clSetKernelArg(mHashMod, 1, sizeof(cl_mem), &hashmod.count.DeviceData);
  clSetKernelArg(mHashMod, 2, sizeof(cl_mem), &hashmod.primorialBitField.DeviceData);
  clSetKernelArg(mHashMod, 3, sizeof(cl_mem), &hashmod.midstate.DeviceData);

  unsigned numhash = 64*262144;

  unsigned hashm[32];
  unsigned foundHashNum = 0;
  memset(hashm, 0, sizeof(hashm));

  while (foundHashNum < 64) {
  {
    sha256precalcData data;
    uint8_t *pHeader = (uint8_t*)&blockheader;
    for (unsigned i = 0; i < sizeof(blockheader); i++)
      pHeader[i] = rand();
    blockheader.version = PrimeMiner::block_t::CURRENT_VERSION;
    blockheader.nonce = 1;    
    precalcSHA256(&blockheader, hashmod.midstate.HostData, &data);
    OCL(hashmod.midstate.copyToDevice(queue));
    OCL(clSetKernelArg(mHashMod, 4, sizeof(cl_uint), &data.merkle));
    OCL(clSetKernelArg(mHashMod, 5, sizeof(cl_uint), &data.time));
    OCL(clSetKernelArg(mHashMod, 6, sizeof(cl_uint), &data.nbits));
    OCL(clSetKernelArg(mHashMod, 7, sizeof(cl_uint), &data.W0));
    OCL(clSetKernelArg(mHashMod, 8, sizeof(cl_uint), &data.W1));
    OCL(clSetKernelArg(mHashMod, 9, sizeof(cl_uint), &data.new1_0));
    OCL(clSetKernelArg(mHashMod, 10, sizeof(cl_uint), &data.new1_1));
    OCL(clSetKernelArg(mHashMod, 11, sizeof(cl_uint), &data.new1_2));
    OCL(clSetKernelArg(mHashMod, 12, sizeof(cl_uint), &data.new2_0));
    OCL(clSetKernelArg(mHashMod, 13, sizeof(cl_uint), &data.new2_1));
    OCL(clSetKernelArg(mHashMod, 14, sizeof(cl_uint), &data.new2_2));
    OCL(clSetKernelArg(mHashMod, 15, sizeof(cl_uint), &data.temp2_3));    
  }    

  OCL(hashmod.count.copyToDevice(queue, false));
    
  size_t globalSize[] = { numhash, 1, 1 };
  size_t localSize[] = { defaultGroupSize, 1 };    
 
  hashmod.count[0] = 0;
  OCL(hashmod.count.copyToDevice(queue));

  {
    cl_event event;
    cl_int result;
    if ((result = clEnqueueNDRangeKernel(queue,
                                         mHashMod,
                                         1,
                                         0,
                                         globalSize,
                                         localSize,
                                         0,
                                         0, &event)) != CL_SUCCESS) {
      LOG_F(ERROR, "[mHashMod] clEnqueueNDRangeKernel error!");
      return;
    }
        
    cl_int error;
    if ((error = clWaitForEvents(1, &event)) != CL_SUCCESS) {
      LOG_F(ERROR, "[mHashMod] clWaitForEvents error %i!", error);
      return;
    }
      
    clReleaseEvent(event);
  }
    
  OCL(hashmod.found.copyToHost(queue, false));
  OCL(hashmod.primorialBitField.copyToHost(queue, false));
  OCL(hashmod.count.copyToHost(queue, false));
  clFinish(queue);

  for(unsigned i = 0; i < hashmod.count[0]; ++i) {
    PrimeMiner::hash_t hash;
    hash.time = blockheader.time;
    hash.nonce = hashmod.found[i];
    uint32_t primorialBitField = hashmod.primorialBitField[i];
    uint32_t primorialIdx = primorialBitField >> 16;
    uint64_t realPrimorial = 1;
    for (unsigned j = 0; j < primorialIdx+1; j++) {
      if (primorialBitField & (1 << j))
        realPrimorial *= gPrimes[j];
    }      
    
    mpz_class mpzRealPrimorial;        
    mpz_import(mpzRealPrimorial.get_mpz_t(), 2, -1, 4, 0, 0, &realPrimorial);            
    primorialIdx = std::max(mPrimorial, primorialIdx) - mPrimorial;
    mpz_class mpzHashMultiplier = allPrimorial[primorialIdx] / mpzRealPrimorial;

    mpz_import(mpzRealPrimorial.get_mpz_t(), 2, -1, 4, 0, 0, &realPrimorial);        
    
    PrimeMiner::block_t b = blockheader;
    b.nonce = hash.nonce;
    
    SHA_256 sha;
    sha.init();
    sha.update((const unsigned char*)&b, sizeof(b));
    sha.final((unsigned char*)&hash.hash);
    sha.init();
    sha.update((const unsigned char*)&hash.hash, sizeof(uint256));
    sha.final((unsigned char*)&hash.hash);

    if(hash.hash < (uint256(1) << 255)){
      LOG_F(ERROR, " * error: hash does not meet minimum");
      continue;
    }
    
    mpz_class mpzHash;
    mpz_set_uint256(mpzHash.get_mpz_t(), hash.hash);
    if(!mpz_divisible_p(mpzHash.get_mpz_t(), mpzRealPrimorial.get_mpz_t())){
      LOG_F(ERROR, " * error: mpz_divisible_ui_p failed");
      continue;
    }    

    mpz_set_uint256(mpzHash.get_mpz_t(), hash.hash);
    hash.primorialIdx = primorialIdx;
    hash.primorial = mpzHashMultiplier;
    hash.shash = mpzHash * hash.primorial;      
    
    unsigned hid = hashes.push(hash);
    if (hid >= 64)
      break;
    memset(&hashBuf[hid*mConfig.N], 0, sizeof(uint32_t)*mConfig.N);
    mpz_export(&hashBuf[hid*mConfig.N], 0, -1, 4, 0, 0, hashes.get(hid).shash.get_mpz_t());        
    foundHashNum = hid + 1;
  }
  }

  OCL(hashBuf.copyToDevice(queue, false));
  
  unsigned MSO = 1024 * mConfig.STRIPES / 2;
  for(int sieveIdx = 0; sieveIdx < 64; ++sieveIdx) {
    for (int pipelineIdx = 0; pipelineIdx < FERMAT_PIPELINES; pipelineIdx++)
      OCL(sieveBuffers[sieveIdx][pipelineIdx].init(context, MSO, CL_MEM_READ_WRITE));
      
    OCL(candidatesCountBuffers[sieveIdx].init(context, FERMAT_PIPELINES, CL_MEM_READ_WRITE));
  }  
  
  for(int k = 0; k < 2; ++k){
    OCL(sieveBuf[k].init(context, mConfig.SIZE*mConfig.STRIPES/2*mConfig.WIDTH, CL_MEM_READ_WRITE));
    OCL(sieveOff[k].init(context, mConfig.PCOUNT*mConfig.WIDTH, CL_MEM_READ_WRITE));
  }  

  clSetKernelArg(mSieveSetup, 0, sizeof(cl_mem), &sieveOff[0].DeviceData);
  clSetKernelArg(mSieveSetup, 1, sizeof(cl_mem), &sieveOff[1].DeviceData);
  clSetKernelArg(mSieveSetup, 3, sizeof(cl_mem), &hashBuf.DeviceData);
  clSetKernelArg(mSieveSearch, 0, sizeof(cl_mem), &sieveBuf[0].DeviceData);
  clSetKernelArg(mSieveSearch, 1, sizeof(cl_mem), &sieveBuf[1].DeviceData);
  clSetKernelArg(mSieveSearch, 7, sizeof(cl_uint), &mDepth);

  unsigned count = checkCandidates ? 1 : std::min(64u, hashmod.count[0]);
  
  clFinish(queue);  
  auto gpuBegin = std::chrono::steady_clock::now();  
  
  for (unsigned i = 0; i < count; i++) {
    cl_int hid = hashes.pop();
    unsigned primorialIdx = hashes.get(hid).primorialIdx;
    clSetKernelArg(mSieveSetup, 2, sizeof(cl_mem), &primeBuf[primorialIdx]);
    clSetKernelArg(mSieveSetup, 5, sizeof(cl_mem), &modulosBuf[primorialIdx].DeviceData);          
    clSetKernelArg(mSieve, 2, sizeof(cl_mem), &primeBuf2[primorialIdx]);        

    {
      clSetKernelArg(mSieveSetup, 4, sizeof(cl_int), &hid);
      size_t globalSize[] = { mConfig.PCOUNT, 1, 1 };
      clEnqueueNDRangeKernel(queue, mSieveSetup, 1, 0, globalSize, 0, 0, 0, 0);
    }

    {
      size_t globalSize[] = { defaultGroupSize*mConfig.STRIPES/2, mConfig.WIDTH };
      size_t localSize[] = { defaultGroupSize, 1 };
      clSetKernelArg(mSieve, 0, sizeof(cl_mem), &sieveBuf[0].DeviceData);
      clSetKernelArg(mSieve, 1, sizeof(cl_mem), &sieveOff[0].DeviceData);      
      OCL(clEnqueueNDRangeKernel(queue, mSieve, 2, 0, globalSize, localSize, 0, 0, 0));
    }
    
    {
      size_t globalSize[] = { defaultGroupSize*mConfig.STRIPES/2, mConfig.WIDTH };
      size_t localSize[] = { defaultGroupSize, 1 };
      clSetKernelArg(mSieve, 0, sizeof(cl_mem), &sieveBuf[1].DeviceData);
      clSetKernelArg(mSieve, 1, sizeof(cl_mem), &sieveOff[1].DeviceData);      
      OCL(clEnqueueNDRangeKernel(queue, mSieve, 2, 0, globalSize, localSize, 0, 0, 0));
    }    

    candidatesCountBuffers[i][0] = 0;
    candidatesCountBuffers[i][1] = 0;    
    OCL(candidatesCountBuffers[i].copyToDevice(queue, false));
        
    {
      cl_uint multiplierSize = mpz_sizeinbase(hashes.get(hid).shash.get_mpz_t(), 2);
      clSetKernelArg(mSieveSearch, 2, sizeof(cl_mem), &sieveBuffers[i][0].DeviceData);
      clSetKernelArg(mSieveSearch, 3, sizeof(cl_mem), &sieveBuffers[i][1].DeviceData);
      clSetKernelArg(mSieveSearch, 4, sizeof(cl_mem), &candidatesCountBuffers[i].DeviceData);
      clSetKernelArg(mSieveSearch, 5, sizeof(cl_int), &hid);
      clSetKernelArg(mSieveSearch, 6, sizeof(cl_uint), &multiplierSize);
      size_t globalSize[] = { mConfig.SIZE*mConfig.STRIPES/2, 1, 1 };
      size_t localSize[] = { 256, 1 };
      OCL(clEnqueueNDRangeKernel(queue, mSieveSearch, 1, 0, globalSize, localSize, 0, 0, 0));
          
      OCL(candidatesCountBuffers[i].copyToHost(queue, false));
    }
  
    if (checkCandidates) {
      OCL(sieveBuf[0].copyToHost(queue));
      OCL(sieveBuf[1].copyToHost(queue));
      OCL(sieveBuffers[i][0].copyToHost(queue));
      OCL(sieveBuffers[i][1].copyToHost(queue));
      clFinish(queue);
      
      std::set<mpz_class> multipliers;
      unsigned invalidCount = 0;
      sieveResultsTest(gPrimes,
                       hashes.get(hid).shash,
                       (uint8_t*)sieveBuf[0].HostData,
                       (uint8_t*)sieveBuf[1].HostData,
                       mConfig.SIZE*32*mConfig.STRIPES/2,
                       mConfig.TARGET,
                       mConfig.PCOUNT,
                       mConfig.WIDTH-mConfig.TARGET,
                       multipliers,
                       &invalidCount);

      unsigned n320 = candidatesCountBuffers[i][0];
      unsigned n352 = candidatesCountBuffers[i][1];
      unsigned diff = 0;
      for (unsigned j = 0; j < n320; j++) {
        PrimeMiner::fermat_t &c = sieveBuffers[i][0].get(j);
        mpz_class X = ((mpz_class)c.index) << c.origin;
        diff += !multipliers.count(X);
      }
      
      for (unsigned j = 0; j < n352; j++) {
        PrimeMiner::fermat_t &c = sieveBuffers[i][1].get(j);
        mpz_class X = ((mpz_class)c.index) << c.origin;
        diff += !multipliers.count(X);
      }      
      
      double coeff = fabs(n320+n352 - multipliers.size()) / multipliers.size();
      if (coeff <= 0.01) {
        LOG_F(INFO, " * [%s] found candidates by CPU: %u by GPU: %u",
              coeff <= 0.01  ? "OK" : "FAILED",
              (unsigned)multipliers.size(),
              n320 + n352);
        LOG_F(INFO, " * [%s] invalid candidates: %u", !invalidCount ? "OK" : "FAILED", invalidCount);
        LOG_F(INFO, " * [%s] CPU/GPU candidates difference: %u", !diff ? "OK" : "FAILED", diff);
      } else {
        LOG_F(ERROR, " * [%s] found candidates by CPU: %u by GPU: %u",
              coeff <= 0.01  ? "OK" : "FAILED",
              (unsigned)multipliers.size(),
              n320 + n352);
        LOG_F(ERROR, " * [%s] invalid candidates: %u", !invalidCount ? "OK" : "FAILED", invalidCount);
        LOG_F(ERROR, " * [%s] CPU/GPU candidates difference: %u", !diff ? "OK" : "FAILED", diff);
      }
    }
  }

  if (!checkCandidates) {
    clFinish(queue);
    auto gpuEnd = std::chrono::steady_clock::now();  
    auto totalTime = std::chrono::duration_cast<std::chrono::microseconds>(gpuEnd-gpuBegin).count();
    double iterationTime = (double)totalTime / count;
    uint64_t bitsInSieve = mConfig.SIZE*32*mConfig.STRIPES/2*mConfig.WIDTH;
    double scanSpeed = bitsInSieve / iterationTime;
  
    unsigned n320 = 0, n352 = 0;
    for (unsigned i = 0; i < count; i++) {
      n320 += candidatesCountBuffers[i][0];
      n352 += candidatesCountBuffers[i][1];
    }

    LOG_F(INFO, " * scan speed: %.3lf G", scanSpeed/1000.0);
    LOG_F(INFO, " * iteration time: %.3lfms", iterationTime/1000.0);
    LOG_F(INFO, " * candidates per second: %.3lf", (n320+n352)/(totalTime/1000000.0));
    LOG_F(INFO, " * candidates per iteration: %.2lf (%.2lf 320bit, %.2lf 352bit)",
           (double)(n320+n352) / count,
           (double)n320 / count,
           (double)n352 / count);
    LOG_F(INFO, " * 320bit/352bit ratio: %.3lf/1", (double)n320/(double)n352);
  }
}

void runBenchmarks(cl_context context,
                   openclPrograms &programs,
                   cl_device_id deviceId,
                   unsigned depth,
                   unsigned defaultGroupSize)
{

  const unsigned mPrimorial = 13;
  char deviceName[128] = {0};
  cl_uint computeUnits;
  clBuffer<config_t> mConfig;
  mpz_class allPrimorials[maxHashPrimorial];

  clGetDeviceInfo(deviceId, CL_DEVICE_NAME, sizeof(deviceName), deviceName, 0);
  clGetDeviceInfo(deviceId, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(computeUnits), &computeUnits, 0);
  LOG_F(INFO, "%s; %u compute units", deviceName, computeUnits);

  cl_int error;
  cl_command_queue queue = clCreateCommandQueue(context, deviceId, 0, &error);
  if (!queue || error != CL_SUCCESS) {
    LOG_F(ERROR, " * Error: can't create command queue");
    return;
  }
  
  // Get miner config
  {
    OCL(mConfig.init(context, 1));
    
    size_t globalSize = 1;
    size_t localSize = 1;
    cl_kernel getconf = clCreateKernel(programs.sieve, "getconfig", &error);
    clSetKernelArg(getconf, 0, sizeof(cl_mem), &mConfig.DeviceData);
    clEnqueueNDRangeKernel(queue, getconf, 1, 0, &globalSize, &localSize, 0, 0, 0);
    OCL(mConfig.copyToHost(queue, true));
    clFinish(queue);
  }  
  
  {
    for (unsigned i = 0; i < maxHashPrimorial - mPrimorial; i++) {
      mpz_class p = 1;
      for(unsigned j = 0; j <= mPrimorial+i; j++)
        p *= gPrimes[j];
      
      allPrimorials[i] = p;
    }    
  }  

  srand(12345);
  multiplyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 320/32, 96/32, 262144, aidMultiply, ttUnitTest);
  multiplyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 320/32, 128/32, 262144, aidMultiply, ttUnitTest);
  multiplyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 320/32, 192/32, 262144, aidMultiply, ttUnitTest);
  multiplyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 352/32, 96/32, 262144, aidMultiply, ttUnitTest);
  multiplyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 352/32, 128/32, 262144, aidMultiply, ttUnitTest);
  multiplyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 352/32, 192/32, 262144, aidMultiply, ttUnitTest);
  multiplyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 320/32, 0, 262144, aidSquare, ttUnitTest);
  multiplyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 640/32, 0, 262144, aidSquare, ttUnitTest);
  multiplyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 320/32, 320/32, 262144, aidMultiply, ttUnitTest);
  multiplyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 640/32, 640/32, 262144, aidMultiply, ttUnitTest);
  monMulBenchmark(context, queue, programs.benchmarks, computeUnits*4, 320/32, 262144, aidMontgomerySquare, ttUnitTest);
  monMulBenchmark(context, queue, programs.benchmarks, computeUnits*4, 320/32, 262144, aidMontgomeryMultiply, ttUnitTest);
  monMulBenchmark(context, queue, programs.benchmarks, computeUnits*4, 320/32, 262144, aidMontgomeryRedchalf, ttUnitTest);
  redcifyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 320/32, 262144, aidRedcify, ttUnitTest);
  monMulBenchmark(context, queue, programs.benchmarks, computeUnits*4, 352/32, 262144, aidMontgomerySquare, ttUnitTest);
  monMulBenchmark(context, queue, programs.benchmarks, computeUnits*4, 352/32, 262144, aidMontgomeryMultiply, ttUnitTest);
  monMulBenchmark(context, queue, programs.benchmarks, computeUnits*4, 352/32, 262144, aidMontgomeryRedchalf, ttUnitTest);
  redcifyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 352/32, 262144, aidRedcify, ttUnitTest);
  divideBenchmark(context, queue, programs.benchmarks, computeUnits*4, 320/32, 262144, ttUnitTest);
  divideBenchmark(context, queue, programs.benchmarks, computeUnits*4, 352/32, 262144, ttUnitTest);

  multiplyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 320/32, 0, 262144, aidSquare, ttPerformanceTest);
  multiplyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 352/32, 0, 262144, aidSquare, ttPerformanceTest);
  multiplyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 640/32, 0, 262144, aidSquare, ttPerformanceTest);
  multiplyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 320/32, 320/32, 262144, aidMultiply, ttPerformanceTest);
  multiplyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 352/32, 352/32, 262144, aidMultiply, ttPerformanceTest);
  multiplyBenchmark(context, queue, programs.benchmarks, computeUnits*4, 640/32, 640/32, 262144, aidMultiply, ttPerformanceTest);

  fermatTestBenchmark(context, queue, programs.benchmarks, computeUnits*4, 320/32, 131072);
  fermatTestBenchmark(context, queue, programs.benchmarks, computeUnits*4, 352/32, 131072);

  hashmodBenchmark(context, queue, programs.sha256, defaultGroupSize, 0, allPrimorials, mPrimorial);
  sieveTestBenchmark(context, queue, programs, defaultGroupSize, computeUnits*4, allPrimorials, mPrimorial, *mConfig.HostData, depth, true);
  sieveTestBenchmark(context, queue, programs, defaultGroupSize, computeUnits*4, allPrimorials, mPrimorial, *mConfig.HostData, depth, false);

  clReleaseCommandQueue(queue);
}
