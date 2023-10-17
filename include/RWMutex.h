#include <semaphore.h>


class RWMutex {
  public:
    RWMutex(uint read_num): MAX_READ_(read_num), is_closed_(false) {
      sem_init(&semId_, 0, MAX_READ_);
    }

    bool ReadLock() {
      sem_wait(&semId_); // 等待 semId_ > 0, semId_ - 1
    }
    bool ReadUnlock() {
      sem_post(&semId_); // 释放 semId_, semId_ + 1
    }

    bool WriteLock() {
      // 拿到所有的资源
      for (uint i = 0; i < MAX_READ_; i++) {
        sem_wait(&semId_);
      }
    }

    bool WriteUnLock() {
      // 释放所有的资源
      for (uint i = 0; i < MAX_READ_; i++) {
        sem_post(&semId_);
      }
    }

    ~RWMutex() {
      // 拿到所有的资源
      for (uint i = 0; i < MAX_READ_; i++) {
        sem_wait(&semId_);
      }
      is_closed_ = true;
      // 释放所有的资源
      for (uint i = 0; i < MAX_READ_; i++) {
        sem_post(&semId_);
      }
      sem_destroy(&semId_);
    }

  private:
    const uint MAX_READ_;
    bool is_closed_;
    sem_t semId_;
};