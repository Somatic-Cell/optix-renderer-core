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

} // namespace

void validate_export_settings(const ExportSettings& settings)
{
    if(settings.path.empty())
    {
        throw std::invalid_argument("Export path must not be empty");
    }

    if(
        settings.path.native().find(std::filesystem::path::value_type{})
        != std::filesystem::path::string_type::npos
    ){
        throw std::invalid_argument("Export path must not contain NUL");
    }

    static_cast<void>(select_writer(settings.format));
    image_processing::validate_output_transform_settings(settings.transform);
}

void validate_export_request(
    const ExportSettings& settings,
    std::uint32_t width,
    std::uint32_t height
)
{
    validate_export_settings(settings);
    select_writer(settings.format).validate_dimensions(width, height);
}

void write_export_image(
    const ExportSettings& settings,
    RGB8ImageView image
)
{
    validate_export_request(settings, image.width, image.height);
    select_writer(settings.format).write(settings.path, image);
}

} // namespace renderer::image_export