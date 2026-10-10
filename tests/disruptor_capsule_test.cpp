#define DISRUPTOR_CAPSULE_NO_HOST 1
#include "../src/disruptor_capsule.cpp"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
using disruptor::capsule::Frame;
int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

bool same(const Frame &one, const Frame &other) {
    return one.pad[0] == other.pad[0] && one.pad[1] == other.pad[1] && one.byte_at == other.byte_at && one.byte == other.byte &&
           std::memcmp(&one.pitch, &other.pitch, sizeof(one.pitch)) == 0 && one.memory == other.memory;
}

void test_frames_survive_the_file() {
    std::vector<Frame> frames(3), back;
    frames[0].pad[0] = 0xBFFE;
    frames[0].pad[1] = 0x1234;
    frames[0].byte_at = 0x80077624u;
    frames[0].byte = 0xC3;
    frames[0].pitch = -17.25f;
    frames[0].memory = 0x0123456789ABCDEFull;
    frames[2].pitch = 3.5f;
    frames[2].memory = 0xFFFFFFFFFFFFFFFFull;
    uint64_t first = 0;
    const std::string bytes = disruptor::capsule::encode(frames, 0x0000000867A1B2C3ull);
    expect(bytes.size() == 20 + 3 * 21 && bytes.compare(0, 8, "DCAPSUL1") == 0 && static_cast<uint8_t>(bytes[8]) == 3 &&
               static_cast<uint8_t>(bytes[12]) == 0xC3 && static_cast<uint8_t>(bytes[16]) == 0x08,
           "the file is a name, a count, the first VBlank's cycle and 21 bytes a frame");
    expect(static_cast<uint8_t>(bytes[20]) == 0xFE && static_cast<uint8_t>(bytes[21]) == 0xBF && static_cast<uint8_t>(bytes[24]) == 0x24 &&
               static_cast<uint8_t>(bytes[27]) == 0x80 && static_cast<uint8_t>(bytes[28]) == 0xC3 && static_cast<uint8_t>(bytes[33]) == 0xEF &&
               static_cast<uint8_t>(bytes[40]) == 0x01,
           "every number is written low byte first, wherever the file was made");
    expect(disruptor::capsule::decode(bytes, back, first) && back.size() == 3 && same(back[0], frames[0]) && same(back[1], frames[1]) && same(back[2], frames[2]) &&
               first == 0x0000000867A1B2C3ull,
           "what was written is what is read");
    expect(same(back[1], Frame{}) && back[1].pad[0] == 0xFFFF, "a frame with nothing pressed has both pads released");

    std::vector<Frame> none;
    expect(disruptor::capsule::decode(disruptor::capsule::encode({}, 7), none, first) && none.empty() && first == 7, "an empty recording is a file too");
    std::string wrong = bytes;
    wrong[0] = 'X';
    expect(!disruptor::capsule::decode(wrong, back, first) && back.empty() && first == 0, "a file of another kind is refused and leaves no frames");
    expect(!disruptor::capsule::decode(bytes.substr(0, bytes.size() - 1), back, first) && !disruptor::capsule::decode(bytes + '\0', back, first) &&
               !disruptor::capsule::decode(bytes.substr(0, 19), back, first) && !disruptor::capsule::decode(bytes.substr(0, 9), back, first) &&
               !disruptor::capsule::decode(bytes.substr(0, 5), back, first) && !disruptor::capsule::decode("", back, first),
           "a file cut short or with more than its count says is refused");
    const std::vector<char> few(bytes.begin(), bytes.begin() + 5), nine(bytes.begin(), bytes.begin() + 9);
    expect(!disruptor::capsule::decode({few.data(), few.size()}, back, first) && !disruptor::capsule::decode({nine.data(), nine.size()}, back, first),
           "and nothing is read past the end of a file shorter than its head");
    std::string more = bytes;
    more[8] = 4;
    expect(!disruptor::capsule::decode(more, back, first), "a count the file does not hold is refused");
}

void test_the_digest_sees_every_byte() {
    std::vector<uint8_t> memory(4099, 0);
    const uint64_t empty = disruptor::capsule::digest(memory.data(), memory.size());
    expect(disruptor::capsule::digest(memory.data(), memory.size()) == empty, "the same memory has the same digest");
    for (const size_t at : {size_t{0}, size_t{7}, size_t{8}, size_t{2048}, size_t{4095}, size_t{4096}, size_t{4098}}) {
        memory[at] = 1;
        expect(disruptor::capsule::digest(memory.data(), memory.size()) != empty, "one changed byte changes the digest, in the last odd bytes too");
        memory[at] = 0;
    }
    memory[16] = 5;
    const uint64_t here = disruptor::capsule::digest(memory.data(), memory.size());
    memory[16] = 0;
    memory[24] = 5;
    expect(disruptor::capsule::digest(memory.data(), memory.size()) != here, "the same byte elsewhere is another digest");
    expect(disruptor::capsule::digest(memory.data(), 4098) != disruptor::capsule::digest(memory.data(), 4099), "the length counts");
}

void test_a_picture_is_a_bitmap() {
    const std::vector<uint8_t> rgb = {10, 20, 30, 40, 50, 60, 70, 80, 90, /* second row */ 1, 2, 3, 4, 5, 6, 7, 8, 9};
    const std::string file = disruptor::capsule::bitmap(rgb, 3, 2);
    const auto byte = [&](size_t at) { return static_cast<uint8_t>(file[at]); };
    expect(file.size() == 54 + 2 * 12 && file.compare(0, 2, "BM") == 0 && byte(2) == 78 && byte(10) == 54 && byte(14) == 40, "a header, then rows padded to four bytes");
    expect(byte(18) == 3 && byte(22) == 2 && byte(26) == 1 && byte(28) == 24 && byte(34) == 24, "three by two, one plane, 24 bits, 24 bytes of picture");
    expect(byte(54) == 3 && byte(55) == 2 && byte(56) == 1 && byte(60) == 9 && byte(63) == 0 && byte(64) == 0 && byte(65) == 0,
           "the last row comes first, blue before red, zeros to the row's end");
    expect(byte(66) == 30 && byte(67) == 20 && byte(68) == 10 && byte(74) == 70, "then the first row");
    expect(disruptor::capsule::bitmap({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, 4, 1).size() == 54 + 12, "a row of four pixels needs no padding");
}
}  // namespace

int main() {
    test_frames_survive_the_file();
    test_the_digest_sees_every_byte();
    test_a_picture_is_a_bitmap();
    if (g_failures) return 1;
    std::cout << "capsule file, digest and picture: PASS\n";
    return 0;
}
