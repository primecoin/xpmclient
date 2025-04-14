#ifndef __GETBLOCKTEMPLATE_H_
#define __GETBLOCKTEMPLATE_H_

#include <curl/curl.h>
#include <pthread.h>

extern "C" {
  #include "blkmaker.h"
  #include "blkmaker_jansson.h"
}

#include "primecoin.h"

class GetBlockTemplateContext {
private:
  void *_log;
  const char *_url;
  const char *_user;
  const char *_password;
  const char *_wallet;
  unsigned _timeout;
  unsigned _blocksNum;
  uint32_t _extraNonce;
  bool _blockTemplateExists;
  
  blktemplate_t **_blocks;
  unsigned _dataId;
  unsigned _height;
  double _difficulty;
  
  pthread_mutex_t _mutex;
  std::string _response;
  
private:
  static void *queryWorkThreadProc(void *arg);
  static size_t curlWriteCallback(void *ptr,
                                  size_t size,
                                  size_t nmemb,
                                  GetBlockTemplateContext *ctx);
  
private:
  void queryWork();
  void updateWork();
  
public:
  GetBlockTemplateContext(void *log,
                          const char *url,
                          const char *user,
                          const char *password,
                          const char *wallet,
                          unsigned timeout,
                          unsigned blocksNum,
                          unsigned extraNonce);
  void run();
  blktemplate_t *get(unsigned blockIdx, blktemplate_t *old, unsigned *dataId, bool *hasChanged);
  unsigned getBlockHeight() { return _blockTemplateExists ? _height : 0; }
  double getDifficulty() { return _blockTemplateExists ? _difficulty : 0.0; }
};

class SubmitContext { 
private:
  void *_log;        
  CURL *curl;
  std::string _response;
  const char *_url;
  const char *_user;
  const char *_password;
  
  static size_t curlWriteCallback(void *ptr,
                                  size_t size,
                                  size_t nmemb,
                                  SubmitContext *ctx);
  
  
public:
  SubmitContext(void *log, const char *url, const char *user, const char *password);
  void submitBlock(blktemplate_t *blockTemplate,
                   const PrimecoinBlockHeader &header,
                   unsigned dataId);
};

void *getBlockTemplateThread(void *arg);

#endif //__GETBLOCKTEMPLATE_H_W
