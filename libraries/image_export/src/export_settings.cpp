#include <renderer/image_export/export_settings.hpp>
#include <renderer/image_io/png_writer.hpp>
#include <stdexcept>

namespace renderer::image_export
{

namespace 
{
struct Writer
{
    void (*validate_dimensions)(std::uint32_t, std::uint32_t);
    void (*write)(const std::filesystem::path&, RGB8ImageView);
};

Writer select_writer(ImageFileFormat format)
{
    switch(format)
    {
        case ImageFileFormat::png:
            return {
                image_io::validate_png_dimensions,
                static_cast<void (*)(const std::filesystem::path&, RGB8ImageView)>(image_io::write_png)
            };
    }

    throw std::invalid_argument("Unsupported image file format");
}

Writer checked_writer(const ExportSettings& settings)
{
    validate_export_path(settings.path);
    const auto writer = select_writer(settings.format);
    image_processing::validate_output_transform_settings(settings.transform);
    return writer;
}

} // namespace

void validate_export_path(const std::filesystem::path& path)
{
    if(path.empty())
    {
        throw std::invalid_argument("Export path must not be empty");
    }

    if(
        path.native().find(std::filesystem::path::value_type{})
        != std::filesystem::path::string_type::npos
    ){
        throw std::invalid_argument("Export path must not contain NUL");
    }

}

void validate_export_settings(const ExportSettings& settings)
{
    static_cast<void>(checked_writer(settings));
}

void validate_export_dimensions(ImageFileFormat format, std::uint32_t width, std::uint32_t height)
{
    select_writer(format).validate_dimensions(width, height);
}

void validate_export_request(
    const ExportSettings& settings,
    std::uint32_t width,
    std::uint32_t height
)
{
    checked_writer(settings).validate_dimensions(width, height);
}

void write_export_image(
    const ExportSettings& settings,
    RGB8ImageView image
)
{
    checked_writer(settings).write(settings.path, image);
}

} // namespace renderer::image_export