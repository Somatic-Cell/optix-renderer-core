#include <renderer/asset_io/png_writer.hpp>

#include <fpng.h>

#include <cstddef>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace renderer::asset_io
{

// fpng を使って PNG ファイルをエンコードし，保存する
void write_png(
    const std::filesystem::path& path,  // 保存先のパス
    const SRGB8Image& image             // エンコードしたい画像
)
{
    // fpng は，IHDR (画像ヘッダ) に書き込む画像のサイズを 16-bit で表現している
    // そのため，fpng の実装内で正しく動かすために 2^16 -1 以上の画素数を受け付けないようにする 
    constexpr std::uint32_t fpng_max_dimension = 65535;
    
    // 保存しようとしている画像サイズが正常に保存できる範囲かどうかを確認
    if(image.width == 0 || image.height == 0 || image.width > fpng_max_dimension || image.height > fpng_max_dimension)
    {
        throw std::invalid_argument("fpng image dimensions must be in [1, 2^16)");
    }

    // ディスクリプタの画素数と実際に格納されている画素数が不整合でないかどうかを確認
    const std::uint64_t pixel_count = static_cast<std::uint64_t>(image.width) * image.height;
    if(pixel_count > std::numeric_limits<std::size_t>::max() / sizeof(RGB8)
        || image.pixels.size() != static_cast<std::size_t>(pixel_count))
    {
        throw std::invalid_argument("sRGB image dimensions do not match its pixel count");
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
            static_cast<std::uint64_t>(image.width) * 3 + 1 // 各行 width x 3 (RGB) 画素に加え, Filter type 1 Byte 分が追加される
        )  * image.height;
    
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

    constexpr auto max_offset = std::numeric_limits<std::uint32_t>::max();
    if(filtered_bytes + 65534 > max_offset || max_png_bytes > max_offset)
    {
        throw std::length_error("Image exceeds fpng's 32-bit buffer limits");
    }

    // PNG の一つのチャンクに入れられるデータ長は 2^32 - 1 byte 以下
    // fpng は zlib データ全体を一つの IDAT に格納するため，これが 2^32 - 1 byte 以下かどうかをチェックする 
    const std::uint64_t max_idat_bytes = 6 + filtered_bytes + block_count * 5;
    constexpr std::uint64_t max_png_chunk_dada_bytes = 0x7fff'ffff; // 2^32 - 1

    if(max_idat_bytes > max_png_chunk_dada_bytes)
    {
        throw std::length_error("Image exceeds fpng's single-IDAT size limit");
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
        image.pixels.data(), image.width, image.height, 3, encoded
    ))
    {
        throw std::runtime_error("fpng failed to encode the PNG image");
    }

    if(encoded.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
    {
        throw std::length_error("Encoded PNG is too large for a single file write");
    }

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


} // namespace renderer::asset_io