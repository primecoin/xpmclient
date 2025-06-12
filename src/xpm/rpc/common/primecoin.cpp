#include "CSieveOfEratosthenesL1Ext.h"
#include "primecoin.h"
#include "system.h"

#include <stdlib.h>
#include <time.h>
#include <limits>

#include <openssl/bn.h>
#include <openssl/sha.h>

#include <algorithm>
#include "prime.h"

extern unsigned gDebug;
extern unsigned gSieveSize;
extern unsigned gWeaveDepth;

uint32_t bitsFromDifficulty(double difficulty)
{
  uint32_t chainLength = (uint32_t)difficulty;
  uint32_t fractionalPart = (uint32_t)
    ((difficulty - (double)chainLength) * (1U << DifficultyFractionalBits));
    
  return (chainLength << DifficultyFractionalBits) | fractionalPart;
}

uint32_t bitsFromChainLengthWithoutFractional(uint32_t chainLength)
{
  return chainLength << DifficultyFractionalBits;
}

uint32_t chainLengthFromBits(uint32_t bits)
{
  return bits >> DifficultyFractionalBits;
}

double difficultyFromBits(uint32_t bits)
{
  return (double)(bits >> DifficultyFractionalBits) +
         ((bits & DifficultyFractionalMask) / 16777216.0);
}

void incrementChainLengthInBits(uint32_t *bits)
{
  *bits += (1 << DifficultyFractionalBits);
}

void generateRandomHeader(PrimecoinBlockHeader *header, double difficulty)
{
  header->version = 2;
  for (unsigned i = 0; i < sizeof(header->hashPrevBlock); i++) {
    header->hashPrevBlock[i] = rand() % 0xFF;
    header->hashMerkleRoot[i] = rand() % 0xFF;
  }
  
  header->time = time(0);
  header->bits = bitsFromDifficulty(difficulty);
  header->nonce = 0;
}

// "Magic number" search function (for replace division to multiplication & shift)
static bool findMultiplierForConstantDivision(unsigned maxDividendBits,
                                              const mpz_class &divisor,
                                              mpz_class *multiplier,
                                              unsigned *offset)
{
  mpz_class two = 2;
  
  mpz_class maxDividend = 1;
  maxDividend <<= maxDividendBits;
  maxDividend--;
  
  mpz_class nc = (maxDividend / divisor) * divisor - 1;
  for (unsigned i = 0; i < 2*maxDividendBits + 1; i++) {
    mpz_class powOf2 = 1;
    powOf2 <<= i;
    if (powOf2 > nc*(divisor - 1 - ((powOf2 - 1) % divisor))) {
      *multiplier = (powOf2 + divisor - 1 - ((powOf2 - 1) % divisor)) / divisor;
      *offset = i;
      return true;
    }
  }
  
  return false;
}

void generatePrimes(uint32_t *out, unsigned primesNum)
{
  size_t index = 0;
  
  std::vector<bool> sieve(primesNum, false);
  for (uint32_t F = 2; F*F < primesNum; F++) {
    if (sieve[F])
      continue;
    for (uint32_t C = F*F; C < primesNum; C += F)
      sieve[C] = true;
  }
  
  for (unsigned int n = 2; n < primesNum; n++) {
    if (!sieve[n])
      out[index++] = n;
  }
}

void mpzExport(const mpz_class &N, uint32_t *limbs)
{
  size_t limbsNumber;
  mpz_export(limbs+1, &limbsNumber, -1, 4, 0, 0, N.get_mpz_t());
  limbs[0] = limbsNumber;
}

void mpzImport(const uint32_t *limbs, mpz_class &N)
{
  mpz_import(N.get_mpz_t(), limbs[0], -1, 4, 0, 0, limbs+1);
}

void generateMultipliers(uint32_t *primesCombined,
                         uint32_t *isNewMultiplier,
                         uint32_t *multipliers,
                         uint32_t *offsets,
                         uint64_t *multipliers64,
                         uint32_t *offsets64,
                         uint64_t *combinedMultipliers,
                         uint32_t *combinedOffsets,
                         uint32_t *primes,
                         unsigned primesNum,
                         unsigned multipliersNum)
{
  unsigned primeCombinedIdx = 0;
  mpz_class divisor;
  mpz_class inversedMultiplier;
  mpz_class inversedMultiplier64;
  mpz_class inversedCombinedMultiplier;
  unsigned offset;
  unsigned offset64;
  unsigned combinedOffset;
  unsigned primeCombined;
  for (unsigned i = 0; i < multipliersNum; i++) {
    findMultiplierForConstantDivision(31, primes[i], &inversedMultiplier, &offset);
    if (offset < 32)
      findMultiplierForConstantDivision(32, primes[i], &inversedMultiplier, &offset);
    
    findMultiplierForConstantDivision(63, primes[i], &inversedMultiplier64, &offset64);    
    if (offset64 < 64)
      findMultiplierForConstantDivision(64, primes[i], &inversedMultiplier64, &offset64);          
    if (primeCombinedIdx <= i) {
      primeCombined = 1;
      while (primeCombined < (1U << 31) / primes[primeCombinedIdx])
        primeCombined *= primes[primeCombinedIdx++];
      
      divisor = primeCombined;
      findMultiplierForConstantDivision(63, divisor, &inversedCombinedMultiplier, &combinedOffset);
      isNewMultiplier[i] = 1;
    } else {
      isNewMultiplier[i] = 0;
    }
    
    size_t size;
    primesCombined[i] = primeCombined;    
    mpz_export(&multipliers[i], &size, -1, 4, 0, 0, inversedMultiplier.get_mpz_t());
    mpz_export(&multipliers64[i], &size, -1, 4, 0, 0, inversedMultiplier64.get_mpz_t());    
    mpz_export(&combinedMultipliers[i], &size, -1, 4, 0, 0, inversedCombinedMultiplier.get_mpz_t());
    offsets[i] = offset;
    offsets64[i] = offset64;
    combinedOffsets[i] = combinedOffset;
  }
}

bool trialDivisionChainTest(const PrimeSource &primeSource,
                            mpz_class &N,
                            bool fSophieGermain,
                            unsigned chainLength,
                            unsigned depth)
{
  N += (fSophieGermain ? -1 : 1);
  for (unsigned i = 0; i < chainLength; i++) {
    for (unsigned divIdx = 0; divIdx < depth; divIdx++) { 
      if (mpz_tdiv_ui(N.get_mpz_t(), primeSource[divIdx]) == 0) {
        fprintf(stderr, " * divisor: [%u]%u\n", divIdx, primeSource[divIdx]);
        return false;
      }
    }
    
    N <<= 1;
    N += (fSophieGermain ? 1 : -1);
  }
  
  return true;
}


PrimeSource::PrimeSource(uint32_t primesNum, unsigned inversedMultipliersNum) :
  _primesNum(primesNum)
{
  // Use Eratosthenes sieve for search first N primes
  _primes = new uint32_t[primesNum];

  _primesCombined = new uint32_t[inversedMultipliersNum];
  _isNewMultiplier = new uint32_t[inversedMultipliersNum];
  _multipliers = new uint32_t[inversedMultipliersNum];
  _offsets = new uint32_t[inversedMultipliersNum];
  _multipliers64 = new uint64_t[inversedMultipliersNum];
  _offsets64 = new uint32_t[inversedMultipliersNum];
  _combinedMultipliers = new uint64_t[inversedMultipliersNum];
  _combinedOffsets = new uint32_t[inversedMultipliersNum];
   
  generatePrimes(_primes, primesNum);
  generateMultipliers(_primesCombined, _isNewMultiplier,
                      _multipliers, _offsets,
                      _multipliers64, _offsets64,
                      _combinedMultipliers, _combinedOffsets,
                      _primes, primesNum, inversedMultipliersNum);
}

std::string GetPrimeChainName(unsigned int nChainType, unsigned int nChainLength) {
 const std::string strLabels[5] = {"NUL", "1CC", "2CC", "TWN", "UNK"};
 char buffer[64];
 std::snprintf(buffer, sizeof(buffer), "%s%s", strLabels[std::min(nChainType, 4u)].c_str(), TargetToString(nChainLength).c_str());
 return std::string(buffer);
}
