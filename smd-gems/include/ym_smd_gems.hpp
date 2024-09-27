#pragma once

#include <cinttypes>
#include <memory>

#include "ym_smd_io.hpp"

namespace ym::gems
{
	class IBank
	{
	public:
		virtual ~IBank() = default;

		virtual void read_settings(bool& out_use_ptr3, bool& out_use_crt, bool& out_use_pal_scale, bool& out_can_play_another_songs) const = 0;

		virtual void set_instruments_offset(size_t in_instruments) = 0;
		virtual void set_envelopes_offset(size_t in_envelopes) = 0;
		virtual void set_sequences_offset(size_t in_sequences) = 0;
		virtual void set_samples_offset(size_t in_samples) = 0;

		virtual std::uint16_t instruments_num() const = 0;
		virtual std::uint16_t sequences_num() const = 0;

		virtual std::int32_t get_instrument_type(std::uint16_t in_instrument) const = 0;
		virtual const std::uint8_t* get_instrument_raw(std::uint16_t in_instrument) const = 0;

		virtual std::vector<std::int32_t> get_sequence_sequences(std::uint32_t in_offset) const = 0;

		virtual smd::io::rom_reader_t create_reader(size_t in_offset) const = 0;
	};

	class IConverter
	{
	public:
		virtual ~IConverter() = default;

		virtual void convert_sequence(const IBank& in_bank, std::uint16_t in_sequence, std::ostream& out_midi_stream, std::ostream& out_info_stream) const = 0;
	};

	std::shared_ptr<IBank> create_bank(smd::io::data_span_t in_data, bool use_ptr3 = false, bool use_crt = false, bool use_pal_scale = false, bool can_play_another_songs = false);
	std::shared_ptr<IConverter> create_midi_converter();
}
