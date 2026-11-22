# ここに登録したレンダラは RENDERER_BUILD_<大文字名> で個別に選べる
foreach(name IN ITEMS triangle)
    string(TOUPPER "${name}" upper)
    option(RENDERER_BUILD_${upper} "Build the ${name} renderer" ON)
    if(RENDERER_BUILD_${upper})
        add_subdirectory(${name})
    endif()
endforeach()
