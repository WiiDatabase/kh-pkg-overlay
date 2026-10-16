#pragma once

class SpinLock {
public:
    void lock() {
        while (__atomic_exchange_n(&m_State, 1, __ATOMIC_ACQUIRE)) {
        }
    }
    void unlock() {
        __atomic_store_n(&m_State, 0, __ATOMIC_RELEASE);
    }

private:
    int m_State = 0;
};

class LockGuard {
public:
    explicit LockGuard(SpinLock &lock) : m_Lock(lock) {
        m_Lock.lock();
    }
    ~LockGuard() {
        m_Lock.unlock();
    }
    LockGuard(const LockGuard &)            = delete;
    LockGuard &operator=(const LockGuard &) = delete;

private:
    SpinLock &m_Lock;
};
