// File circulaire mono-producteur / mono-consommateur, sans verrou.
// Utilisee entre le callback temps reel PortAudio et le thread reseau.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

class RingBuffer {
public:
    explicit RingBuffer(size_t capacitySamples = 48000)
        : m_buf(capacitySamples), m_cap(capacitySamples) {}

    void reset()
    {
        m_read.store(0, std::memory_order_relaxed);
        m_write.store(0, std::memory_order_relaxed);
    }

    size_t available() const
    {
        const size_t w = m_write.load(std::memory_order_acquire);
        const size_t r = m_read.load(std::memory_order_acquire);
        return (w >= r) ? (w - r) : (m_cap - r + w);
    }

    size_t freeSpace() const { return m_cap - available() - 1; }

    // Renvoie le nombre d'echantillons reellement ecrits.
    size_t write(const int16_t *src, size_t n)
    {
        const size_t space = freeSpace();
        if (n > space) n = space;
        size_t w = m_write.load(std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i) {
            m_buf[w] = src[i];
            if (++w == m_cap) w = 0;
        }
        m_write.store(w, std::memory_order_release);
        return n;
    }

    // Complete avec des zeros si la file est trop courte.
    size_t readOrSilence(int16_t *dst, size_t n)
    {
        const size_t avail = available();
        const size_t take = (n < avail) ? n : avail;
        size_t r = m_read.load(std::memory_order_relaxed);
        for (size_t i = 0; i < take; ++i) {
            dst[i] = m_buf[r];
            if (++r == m_cap) r = 0;
        }
        m_read.store(r, std::memory_order_release);
        if (take < n) std::memset(dst + take, 0, (n - take) * sizeof(int16_t));
        return take;
    }

    size_t read(int16_t *dst, size_t n)
    {
        const size_t avail = available();
        if (n > avail) n = avail;
        size_t r = m_read.load(std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i) {
            dst[i] = m_buf[r];
            if (++r == m_cap) r = 0;
        }
        m_read.store(r, std::memory_order_release);
        return n;
    }

private:
    std::vector<int16_t> m_buf;
    size_t m_cap;
    std::atomic<size_t> m_read{0};
    std::atomic<size_t> m_write{0};
};
