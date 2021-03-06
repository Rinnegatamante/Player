/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * EasyRPG Player is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EasyRPG Player. If not, see <http://www.gnu.org/licenses/>.
 */

#include "system.h"

#ifdef WANT_FASTWAV

// Headers
#include <cstring>
#include "decoder_wav.h"
#include "utils.h"

WavDecoder::WavDecoder()
{
	music_type = "wav";
}

WavDecoder::~WavDecoder() {
}

bool WavDecoder::Open(Filesystem_Stream::InputStream stream) {
	decoded_samples = 0;
	this->stream = std::move(stream);
	this->stream.seekg(16, std::ios_base::beg);
	this->stream.read(reinterpret_cast<char*>(&chunk_size), sizeof(chunk_size));
	Utils::SwapByteOrder(chunk_size);
	this->stream.seekg(2, std::ios_base::cur);
	this->stream.read(reinterpret_cast<char*>(&nchannels), sizeof(nchannels));
	Utils::SwapByteOrder(nchannels);
	this->stream.read(reinterpret_cast<char*>(&samplerate), sizeof(samplerate));
	Utils::SwapByteOrder(samplerate);
	this->stream.seekg(6, std::ios_base::cur);
	uint16_t bitspersample;
	this->stream.read(reinterpret_cast<char*>(&bitspersample), sizeof(bitspersample));
	Utils::SwapByteOrder(bitspersample);
	switch (bitspersample) {
		case 8:
			output_format=Format::U8;
			break;
		case 16:
			output_format=Format::S16;
			break;
		case 32:
			output_format=Format::S32;
			break;
		default:
			return false;
	}

	// Skip to next chunk using "fmt" chunk as offset
	this->stream.seekg(12 + 8 + chunk_size, std::ios_base::beg);

	char chunk_name[4] = {0};
	this->stream.read(reinterpret_cast<char*>(chunk_name), sizeof(chunk_name));

	// Skipping to audiobuffer start
	while (strncmp(chunk_name, "data", 4)) {
		this->stream.read(reinterpret_cast<char*>(&chunk_size), sizeof(chunk_size));
		Utils::SwapByteOrder(chunk_size);
		this->stream.seekg(chunk_size, std::ios_base::cur);
		this->stream.read(reinterpret_cast<char*>(chunk_name), sizeof(chunk_name));

		if (!this->stream.good()) {
			return false;
		}
	}

	// Get data chunk size
	this->stream.read(reinterpret_cast<char*>(&chunk_size), sizeof(chunk_size));
	Utils::SwapByteOrder(chunk_size);

	if (!this->stream.good()) {
		return false;
	}

	// Start of data chunk
	audiobuf_offset = this->stream.tellg();
	cur_pos = audiobuf_offset;
	finished = false;
	return this->stream.good();
}

bool WavDecoder::Seek(std::streamoff offset, std::ios_base::seekdir origin) {
	finished = false;
	if (!stream)
		return false;
	if (origin == std::ios_base::beg) {
		offset += audiobuf_offset;
	}
	// FIXME: Proper sample count for seek
	decoded_samples = 0;

	bool success = stream.seekg(offset, origin).good();

	if (!success) { stream.clear(); }
	cur_pos = stream.tellg();
	return success;
}

bool WavDecoder::IsFinished() const {
	return finished;
}

void WavDecoder::GetFormat(int& frequency, AudioDecoder::Format& format, int& channels) const {
	if (!stream) return;
	frequency = samplerate;
	channels = nchannels;
	format = output_format;
}

bool WavDecoder::SetFormat(int, AudioDecoder::Format, int) {
	return false;
}

int WavDecoder::FillBuffer(uint8_t* buffer, int length) {
	if (!stream)
		return -1;

	int real_length;

	// Handle case that another chunk is behind "data" or file ended
	if (cur_pos + length >= audiobuf_offset + chunk_size) {
		real_length = audiobuf_offset + chunk_size - cur_pos;
		cur_pos = audiobuf_offset + chunk_size;
	} else {
		real_length = length;
		cur_pos += length;
	}

	if (real_length == 0) {
		finished = true;
		return 0;
	}

	int decoded = stream.read(reinterpret_cast<char*>(buffer), real_length).gcount();

	if (output_format == AudioDecoder::Format::S16) {
		if (Utils::IsBigEndian()) {
			uint16_t *buffer_16 = reinterpret_cast<uint16_t *>(buffer);
			for (int i = 0; i < decoded / 2; ++i) {
				Utils::SwapByteOrder(buffer_16[i]);
			}
		}
		decoded_samples += (decoded / 2);
	} else if (output_format == AudioDecoder::Format::S32) {
		if (Utils::IsBigEndian()) {
			uint32_t *buffer_32 = reinterpret_cast<uint32_t *>(buffer);
			for (int i = 0; i < decoded / 4; ++i) {
				Utils::SwapByteOrder(buffer_32[i]);
			}
		}
		decoded_samples += (decoded / 4);
	}

	if (decoded < length)
		finished = true;

	return decoded;
}

int WavDecoder::GetTicks() const {
	return decoded_samples / (samplerate * nchannels);
}

#endif
