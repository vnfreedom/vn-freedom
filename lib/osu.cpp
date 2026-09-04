#include <maniac/osu.h>
#include <maniac/osu/signatures.h>
#include <thread>
#include <stdexcept>

using namespace osu;

Osu::Osu() : Process("osu!.exe") {
    time_address = read_memory<uintptr_t>(find_signature(signatures::time));
    debug("found time address: %#x", time_address);
    player_pointer = read_memory<uintptr_t>(find_signature(signatures::player));
    debug("found player pointer: %#x", player_pointer);
    status_pointer = read_memory<uintptr_t>(find_signature(signatures::status));
    debug("found status pointer: %#x", status_pointer);
}

Osu::~Osu() = default;

std::string Osu::get_key_subset(const std::string &keys, int column_count) {
	if (column_count > 9) {
		throw std::runtime_error("maps with more than 9 columns are not supported");
	}
	if (column_count <= 0) {
		throw std::runtime_error("got negative column count");
	}
	const size_t key_subset_len = column_count + 1;
	char *const key_subset = reinterpret_cast<char *>(malloc(key_subset_len));
	if (!key_subset) {
		throw std::runtime_error("failed allocating memory");
	}
	key_subset[column_count] = '\0';
	const size_t subset_offset = (keys.length() / 2) - (column_count / 2);
	memmove_s(key_subset, key_subset_len, reinterpret_cast<const void *>(keys.data() + subset_offset),
		keys.length() - (subset_offset * 2));
	if (column_count % 2) {
		auto offset = column_count / 2;
		memmove_s(key_subset + offset + 1, key_subset_len + offset + 1, key_subset + offset,
			offset);
		key_subset[column_count / 2] = ' ';
	}
	auto out_string = std::string{key_subset};
	free(key_subset);
	return out_string;
}

int32_t Osu::get_game_time() {
    int32_t time = -1;
    if (!read_memory<int32_t>(time_address, &time)) {
        debug("failed getting game time at %#x", (unsigned int)(time_address));
    }
    return time;
}

bool Osu::is_playing() {
    int status = -1;
    if (!read_memory<int>(status_pointer, &status)) {
        debug("failed reading status at %#x (%d)", (unsigned int)(status_pointer), GetLastError());
        return false;
    }
    return status == internal::OSU_STATUS_PLAYING;
}

std::vector<HitObject> Osu::get_hit_objects() {
    internal::process = this;

    constexpr int max_retries = 5;
    bool dumped = false;

    for (int attempt = 0; attempt < max_retries; attempt++) {
        const auto player_address = read_memory_safe<uintptr_t>("player", player_pointer);

        try {
            const auto player = internal::map_player(player_address);
            return player.manager.list.content;
        } catch (std::runtime_error &err) {
            debug("failed getting map player (attempt %d/%d): %s", attempt + 1, max_retries, err.what());

            if (!dumped) {
                dumped = true;
                debug("=== SCANNING FOR HIT OBJECTS LIST (player at %#x) ===", (unsigned)player_address);
                for (uintptr_t off1 = 0x04; off1 <= 0x100; off1 += 0x04) {
                    uintptr_t ptr1 = 0;
                    if (!read_memory(player_address + off1, &ptr1) || ptr1 < 0x10000 || ptr1 > 0x7FFFFFFF) continue;

                    for (uintptr_t off2 = 0x04; off2 <= 0x80; off2 += 0x04) {
                        uintptr_t ptr2 = 0;
                        if (!read_memory(ptr1 + off2, &ptr2) || ptr2 < 0x10000 || ptr2 > 0x7FFFFFFF) continue;

                        uintptr_t arr_ptr = 0;
                        int32_t size8 = 0, sizeC = 0;
                        read_memory(ptr2 + 0x04, &arr_ptr);
                        read_memory(ptr2 + 0x08, &size8);
                        read_memory(ptr2 + 0x0C, &sizeC);

                        if (arr_ptr > 0x10000 && arr_ptr < 0x7FFFFFFF &&
                            ((size8 > 0 && size8 < 100000) || (sizeC > 0 && sizeC < 100000))) {
                            debug("  HIT: player+0x%02x -> +0x%02x -> list (arr=%#x, size@8=%d, size@C=%d)",
                                (unsigned)off1, (unsigned)off2, (unsigned)arr_ptr, size8, sizeC);
                        }
                    }
                }
                debug("=== END SCAN ===");
            }

            player_pointer = read_memory<uintptr_t>(find_signature(signatures::player));
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    throw std::runtime_error("failed getting hit objects after max retries");
}
