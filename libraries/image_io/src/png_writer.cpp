#include <renderer/image_io/png_writer.hpp>

#include <fpng.h>

#include <cstddef>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace renderer::image_io
{

void validate_png_dimensions(std::uint32_t width, std::uint32_t height)
{

    // fpng は，IHDR (画像ヘッダ) に画像の幅と高さの下位 16-bit を書き込む仕様になっているため，
    // 幅と高さがそれぞれ 2^16 -1 を超えてしまわないようにする 
    constexpr std::uint32_t fpng_max_dimension = 65535;
    
    // 保存しようとしている画像サイズが正常に保存できる範囲かどうかを確認
    if(width == 0 || height == 0 || width > fpng_max_dimension || height > fpng_max_dimension)
    {
        throw std::invalid_argument("fpng image dimensions must be in [1, 2^16)");
    }

    // ----------------
    // コーデックを呼ぶ前に，最悪のケースのファイルサイズを確認する
    // fpng は 32-bit のオフセットを使う
    // ----------------

    // png では，画素のバイト列を圧縮する前に，圧縮効率を高めるため，周囲の画素との差分をとるなどの可逆変換を行う
    // この処理をフィルタ処理という
    // フィルタ処理にはいくつかの方法があり，この処理方法を Filter type として各行の先頭に 1Byte メタデータとして置く
    // 例: 0: None, 1: Sub, 2: Up, ...
    const std::uint64_t filtered_bytes = (
            static_cast<std::uint64_t>(width) * 3 + 1 // 各行 width x 3 (RGB) 画素に加え, Filter type 1 Byte 分が追加される
        )  * height;
    
    // fpng では，圧縮を試みた後，Deflate の非圧縮ブロック形式に切り替える
    // Deflate は圧縮データの形式の一つ
    // 非圧縮ブロックには，データ長を表す 16-bit の LEN があるため，1ブロックが保持できるデータは最大で 65,535-Byte
    // 必要となる非圧縮ブロックの個数を，切り上げ除算で求める
    const std::uint64_t block_count = (filtered_bytes + 65534) / 65535;
    
    // 非圧縮が選択された場合の最悪バイト数
    // 58:              PNG ファイルの先頭から，IDAT のデータが始まる直前までのバイト数
    // 6:               zlib のヘッダ 2-byte + 末尾のチェックサム (Adler-32) 4-byte
    // filtered_bytes:  フィルタ後の画素列のバイト数
    // block_count * 5: 各非圧縮 Deflate ブロックにつける管理情報 (BFINAL: 1-byte, LEN: 2-byte, NLEN: 2-byte)
    // 16:              IDAT の CRC 4 byte と終端の IEND チャンク 12 byte
    const std::uint64_t max_png_bytes = 58 + 6 + filtered_bytes + block_count * 5 + 16;

    constexpr auto max_offset = (std::numeric_limits<std::uint32_t>::max)();
    if(filtered_bytes + 65534 > max_offset || max_png_bytes > max_offset)
    {
        throw std::length_error("Image exceeds fpng's 32-bit buffer limits");
    }

    // PNG の一つのチャンクに入れられるデータ長は 2^31 - 1 byte 以下
    // fpng は zlib データ全体を一つの IDAT に格納するため，これが 2^31 - 1 byte 以下かどうかをチェックする 
    const std::uint64_t max_idat_bytes = 6 + filtered_bytes + block_count * 5;
    constexpr std::uint64_t max_png_chunk_dada_bytes = 0x7fff'ffff; // 2^31 - 1

    if(max_idat_bytes > max_png_chunk_dada_bytes)
    {
        throw std::length_error("Image exceeds fpng's single-IDAT size limit");
    }
}

namespace {
std::vector<std::uint8_t> encode_png(RGB8ImageView image)
{
    validate_png_dimensions(image.width, image.height);

    // ディスクリプタの画素数と実際に格納されている画素数が不整合でないかどうかを確認
    const std::uint64_t pixel_count = static_cast<std::uint64_t>(image.width) * image.height;
    if(pixel_count > (std::numeric_limits<std::size_t>::max)() / sizeof(RGB8)
        || image.bytes.size() != static_cast<std::size_t>(pixel_count) * sizeof(RGB8))
    {
        throw std::invalid_argument("sRGB image dimensions do not match its pixel count");
    }
    
    // ----------------
    // fpng の初期化
    // ----------------
    static std::once_flag initialization;
    std::call_once(initialization, [] { fpng::fpng_init(); });

    // ----------------
    // エンコード
    // ----------------
    std::vector<std::uint8_t> encoded;
    if(!fpng::fpng_encode_image_to_memory(
        image.bytes.data(), image.width, image.height, 3, encoded
    ))
    {
        throw std::runtime_error("fpng failed to encode the PNG image");
    }

    if(encoded.size() > static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)()))
    {
        throw std::length_error("Encoded PNG is too large for a single file write");
    }

    return encoded;
}
} // namespace

// fpng を使って PNG ファイルをエンコードし，保存する
void write_png(
    const std::filesystem::path& path,  // 保存先のパス
    const RGB8ImageView image          // エンコードしたい画像
)
{
    const auto encoded = encode_png(image);   

    // ----------------
    // 保存
    // ----------------
    // fpng の char* API は使いにくいので
    // filesystem::path で保存する 
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if(!output)
    {
        throw std::runtime_error("Cannot open the output PNG file");
    }
    output.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    output.close();

    if(!output)
    {
        throw std::runtime_error("Failed to write or close the output PNG file");
    }
}

void write_png(const std::filesystem::path& path, const RGB8Image& image)
{
    write_png(path, RGB8ImageView{image.width, image.height, std::as_bytes(std::span(image.pixels))});
}

std::filesystem::path write_new_png(
    const std::filesystem::path& requested,
    RGB8ImageView image
)
{
    if(
        !requested.has_filename() 
        || requested.filename() == "." 
        || requested.filename() == ".."
        || requested.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos
    )
    {
        throw std::invalid_argument("A PNG filename is required");
    }
    const auto encoded = encode_png(image);
    // TODO: for ループではなく，内部にカウンタを持たせる
    for(int suffix = 0; suffix <= 9999; ++suffix)
    {
        auto path = requested;
        if (suffix != 0)
        {
            auto name = requested.stem();
            const auto digits = std::to_string(suffix);
            name += "_";
            name += std::string(4 - digits.size(), '0');
            name += digits;
            name += requested.extension();
            path.replace_filename(name);
        }

        std::FILE* file = nullptr;
#ifdef _WIN32
        const int error = ::_wfopen_s(&file, path.c_str(), L"wbx");
#else
        file = std::fopen(path.c_str(), "wbx");
        const int error = file ? 0 : errno;
#endif
        if(error == EEXIST) continue;
        if(!file)
        {
            throw std::filesystem::filesystem_error("Cannot create PNG", path, {error, std::generic_category()});
        }

        const auto written = std::fwrite(encoded.data(), 1, encoded.size(), file);
        
        const int write_error = written == encoded.size() ? 0 : (errno ? errno: EIO);
        const int close_error = std::fclose(file) == 0 ? 0 : (errno ? errno: EIO);
        if(write_error || close_error)
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            throw std::filesystem::filesystem_error("Cannot finish PNG", path, 
                {write_error ? write_error : close_error, std::generic_category()}); 
        }

        return path;

    }
    throw std::filesystem::filesystem_error("NO unused PNG filename", requested, std::make_error_code(std::errc::file_exists));

}

} // namespace renderer::asset_io