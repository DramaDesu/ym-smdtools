#include <cassert>
#include <optional>
#include <vector>

#include "ym_smd_io.hpp"
#include "include/ym_smd_gems.hpp"

namespace 
{
	struct BankOffsets
	{
		const std::uint8_t* instruments = nullptr;
		const std::uint8_t* envelopes = nullptr;
		const std::uint8_t* sequences = nullptr;
		const std::uint8_t* samples = nullptr;
	};

	std::uint32_t get_instrument_offset(const std::uint8_t* in_instruments, std::int32_t in_instrument, size_t in_data_size, bool in_clamp = false)
	{
		const auto instrument_offset = in_instruments[2 * in_instrument] + (in_instruments[2 * in_instrument + 1] << 8);
		if (in_clamp && instrument_offset >= in_data_size)
		{
			return 0;
		}
		return instrument_offset;
	}

	class BankImpl : public ym::gems::IBank
	{
	public:
		BankImpl(ym::smd::io::data_span_t in_data, bool in_use_ptr3, bool in_use_crt, bool in_use_pal_scale, bool in_can_play_another_songs)
			: data_(std::move(in_data))
			, use_ptr3_(in_use_ptr3)
			, use_crt_(in_use_crt)
			, use_pal_scale_(in_use_pal_scale)
			, can_play_another_songs_(in_can_play_another_songs)
		{}

		void read_settings(bool& out_use_ptr3, bool& out_use_crt, bool& out_use_pal_scale, bool& out_can_play_another_songs) const override
		{
			out_use_ptr3 = use_ptr3_;
			out_use_crt = use_crt_;
			out_use_pal_scale = use_pal_scale_;
			out_can_play_another_songs = can_play_another_songs_;
		}

		void set_instruments_offset(size_t in_instruments) override
		{
			offsets_.instruments = data_->data() + in_instruments;	
		}
		void set_envelopes_offset(size_t in_envelopes) override
		{
			offsets_.envelopes = data_->data() + in_envelopes;
		}
		void set_sequences_offset(size_t in_sequences) override
		{
			offsets_.sequences = data_->data() + in_sequences;
			sequences_num_.reset();
		}
		void set_samples_offset(size_t in_samples) override
		{
			offsets_.samples = data_->data() + in_samples;
		}
		std::uint16_t instruments_num() const override
		{
			return 0;
		}

		std::int32_t get_instrument_type(std::uint16_t in_instrument) const override
		{
			if (offsets_.instruments != nullptr)
			{
				const auto instrument_offset = get_instrument_offset(offsets_.instruments, in_instrument, data_->size());
				if (instrument_offset < data_->size())
				{
					return offsets_.instruments[instrument_offset];
				}
			}
			return -1;
		}

		const std::uint8_t* get_instrument_raw(std::uint16_t in_instrument) const override
		{
			if (offsets_.instruments != nullptr)
			{
				const auto instrument_offset = get_instrument_offset(offsets_.instruments, in_instrument, data_->size(), true);
				return offsets_.instruments + instrument_offset;
			}
			return nullptr;
		}

		std::uint16_t sequences_num() const override
		{
			if (!sequences_num_.has_value() && offsets_.sequences != nullptr)
			{
				sequences_num_ = ((offsets_.sequences[1] << 8) + offsets_.sequences[0]) / 2;
			}
			return sequences_num_.value_or(0);
		}

		std::vector<std::int32_t> get_sequence_sequences(std::uint32_t in_offset) const override
		{
			std::vector<std::int32_t> out_sequences_offsets;

			if (offsets_.sequences != nullptr)
			{
				const auto sequence_offset = (offsets_.sequences[in_offset + 1] << 8) + offsets_.sequences[in_offset];
				const auto sequences_num = offsets_.sequences[sequence_offset];
				if (sequences_num > 0)
				{
					out_sequences_offsets.reserve(sequences_num);

					for (int i = 0; i < sequences_num; ++i)
					{
						std::int32_t target_offset;
						if (use_ptr3_)
						{
							const auto offset = sequence_offset + 1 + i * 3;
							target_offset = offsets_.sequences[offset] + (offsets_.sequences[offset + 1] << 8) + (offsets_.sequences[offset + 2] << 16);
						}
						else
						{
							const auto offset = sequence_offset + 1 + i * 2;
							target_offset = offsets_.sequences[offset] + (offsets_.sequences[offset + 1] << 8);
						}

						out_sequences_offsets.push_back(target_offset);
					}
				}
			}

			return out_sequences_offsets;
		}

		ym::smd::io::rom_reader_t create_reader(size_t in_offset) const override
		{
			return ym::smd::io::create_rom_reader(data_, in_offset);
		}

	private:
		ym::smd::io::data_span_t data_;
		bool use_ptr3_;
		bool use_crt_;
		bool use_pal_scale_;
		bool can_play_another_songs_;

		BankOffsets offsets_;

		mutable std::optional<std::uint16_t> sequences_num_;
	};
}

namespace ym::gems
{
	std::shared_ptr<IBank> create_bank(smd::io::data_span_t in_data, bool use_ptr3, bool use_crt, bool use_pal_scale, bool can_play_another_songs)
	{
		return std::make_shared<BankImpl>(std::move(in_data), use_ptr3, use_crt, use_pal_scale, can_play_another_songs);
	}
}
