#pragma once

#include <chrono>
#include <vector>
#include <string>
#include <maniac/process.h>
#include <maniac/osu/signatures.h>

namespace osu {
	namespace internal {
		#include <maniac/osu/internal.h>
	};
    using HitObject = internal::hit_object;

	class Osu : public Process {
		uintptr_t time_address = 0;
		uintptr_t player_pointer = 0;
        uintptr_t status_pointer = 0;

    public:
		Osu();
		~Osu();

		int32_t get_game_time();
		bool is_playing();

		std::vector<HitObject> get_hit_objects();
        static std::string get_key_subset(const std::string &keys, int column_count);
	};
}
