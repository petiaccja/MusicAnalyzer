#pragma once

#include <vector>


class ConvolutionBuffer {
public:
	ConvolutionBuffer(size_t size = 1) {
		SetSize(size);
	}

	void SetSize(size_t size = 1) {
		buffer.resize(2 * size, 0.0f);
		m_bufferBegin = buffer.data();
		m_bufferEnd = buffer.data() + buffer.size();
		m_cursor = m_bufferBegin + size;
		m_size = size;
	}
	size_t GetSize() const { return m_size; }


	void AddSamples(float* samples, size_t count) {
		ptrdiff_t space = m_bufferEnd - m_cursor;

		if (count >= m_size) {
			samples += count - m_size;
			count = m_size;
			m_cursor = m_bufferBegin;
			memcpy(m_bufferBegin, samples, m_size * sizeof(float));
			m_cursor += m_size;
			return;
		}
		else if (space <= count) {
			m_cursor -= m_size;
			memcpy(m_cursor, samples, count * sizeof(float));
			m_cursor += count;
		}
		else { // space > count && count < mySize
			memcpy(m_cursor, samples, count * sizeof(float));
			memcpy(m_cursor - m_size, samples, count * sizeof(float));
			m_cursor += count;
		}
	}
	inline void AddSample(float sample) {
		if (m_cursor >= m_bufferEnd) {
			m_cursor -= m_size;
		}
		*(m_cursor - m_size) = sample;
		*(m_cursor) = sample;
	}

	float* GetSamples() {
		return m_cursor - m_size;
	}

	const float* GetSamples() const {
		return m_cursor - m_size;
	}
private:
	std::vector<float> buffer;
	size_t m_size;
	float* m_bufferBegin;
	float* m_bufferEnd;
	float* m_cursor;
};
