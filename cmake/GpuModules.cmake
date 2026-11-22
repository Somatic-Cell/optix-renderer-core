# OptiX IR / fatbin を CUDA ファイルから生成する
# target: 作成する CMake ターゲットの名前
# source: コンパイルする CUDA ソースコード
include_guard(GLOBAL)

function(renderer_configure_gpu_module target)
    cmake_parse_arguments(PARSE_ARGV 1 module "" "STAGE_AS" "")
    if(
        NOT module_STAGE_AS 
        OR module_UNPARSED_ARGUMENTS 
        OR IS_ABSOLUTE "${module_STAGE_AS}"
        OR module_STAGE_AS MATCHES [[\\|[<>:"|?*]|(^|/)[.][.](/|$)|(^|/)[.]$|/$]]
    )
        message(FATAL ERROR "${target}: STAGE_AS requires a path relative to modules directory")
    endif()

    set_target_properties(${target} PROPERTIES
        RENDERER_MODULE_PATH "${module_STAGE_AS}"
        CUDA_RUNTIME_LIBRARY None           # CUDA Runtime library をリンクしない
        CUDA_STANDARD 20                    # C++ 20 を要求
        CUDA_STANDARD_REQUIRED ON           # C++ 20 を満たせない場合に古いバージョンへフォールバックすることを許さない
        CUDA_EXTENSIONS OFF                 # コンパイラ固有の拡張を要求しない
    )

    # NVCC に渡すオプション
    target_compile_options(
        ${target} PRIVATE 
        --generate-line-info    # デバイスコードの行番号を生成する
    )
    if(MSVC)
        target_compile_options(
            ${target} PRIVATE
            "-Xcompiler=/Zc:preprocessor,/utf-8,/Zc:__cplusplus" 
        )
    endif()
endfunction()

function(renderer_add_optix_module target source)
    # コンパイル結果そのものをファイルとして取得したいため，OBJECT を選択
    add_library(${target} OBJECT "${source}")
    renderer_configure_gpu_module(${target} ${ARGN})
    set_target_properties(${target} PROPERTIES
        CUDA_OPTIX_COMPILATION ON           # OptiX IR にコンパイルする
        CUDA_SEPARABLE_COMPILATION ON       # 個々のソースファイルを独立した OptiX のモジュールとして生成するので OFF (Callable のときは ON?)
    )
    target_include_directories(${target} PRIVATE "${OPTIX_INCLUDE_DIR}")
endfunction()

function(renderer_add_cuda_module target source)
    # コンパイル結果そのものをファイルとして取得したいため，OBJECT を選択
    add_library(${target} OBJECT "${source}")
    renderer_configure_gpu_module(${target} ${ARGN})
    set_target_properties(${target} PROPERTIES
        CUDA_FATBIN_COMPILATION ON          # fatbin にコンパイルする
        CUDA_SEPARABLE_COMPILATION OFF      # 個々のソースファイルを独立した fatbin のモジュールとして生成するので OFF (Callable のときは ON?)
    )
endfunction()

# GPU モジュールを読み込むライブラリで，ビルド依存と C++ 用の相対パスを宣言する
function(renderer_require_gpu_module library module path_definition)
    get_target_property(relative_path ${module} RENDERER_MODULE_PATH)
    if(NOT relative_path)
        message(FATAL_ERROR "${library}: ${module} has no GPU module staging path")
    endif()
    get_target_property(module_target ${module} NAME)
    set_property(TARGET ${library} APPEND PROPERTY RENDERER_GPU_MODULES ${module_target})
    target_compile_definitions(${library} PRIVATE "${path_definition}=\"${relative_path}\"")
    add_dependencies(${library} ${module})
endfunction()

# 実行時には静的ライブラリの PRIVATE なリンク先が使うモジュールも必要になる
# プロジェクト内のリンク先はターゲット名（またはそのエイリアス）で指定する
function(_renderer_collect_gpu_modules result)
    set(pending ${ARGN})
    set(visited)
    set(modules)
    while(pending)
        list(POP_BACK pending library)
        if(NOT TARGET "${library}" OR library IN_LIST visited)
            continue()
        endif()
        list(APPEND visited "${library}")
        get_property(required TARGET "${library}" PROPERTY RENDERER_GPU_MODULES)
        list(APPEND modules ${required})
        get_property(direct TARGET "${library}" PROPERTY LINK_LIBRARIES)
        get_property(interface TARGET "${library}" PROPERTY INTERFACE_LINK_LIBRARIES)
        list(APPEND pending ${direct} ${interface})
    endwhile()
    list(REMOVE_DUPLICATES modules)
    set(${result} "${modules}" PARENT_SCOPE)
endfunction()

function(renderer_add_application application)
    cmake_parse_arguments(PARSE_ARGV 1 app "" "" "SOURCES;LINK")
    add_executable(${application} ${app_SOURCES})
    renderer_configure_host(${application})
    target_link_libraries(${application} PRIVATE ${app_LINK})
    set_target_properties(${application} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/${application}")
    set(directory "$<TARGET_FILE_DIR:${application}>/modules")
    target_compile_definitions(${application} PRIVATE "RENDERER_MODULE_DIRECTORY=\"${directory}\"")

    _renderer_collect_gpu_modules(modules ${app_LINK})
    set(destinations)
    foreach(module IN LISTS modules)
        get_target_property(relative_path ${module} RENDERER_MODULE_PATH)
        # Windows でも別のモジュールを上書きしないよう，大文字・小文字を区別せずに照合する
        string(TOLOWER "${relative_path}" destination)
        if(destination IN_LIST destinations)
            message(FATAL_ERROR "${application}: multiple GPU modules would be staged as '${relative_path}'")
        endif()
        list(APPEND destinations "${destination}")
        cmake_path(GET relative_path PARENT_PATH parent)
        set(stage "${application}_${module}_stage")
        # 実行ファイルを再リンクしないビルドでも，削除・更新されたモジュールを配置する
        add_custom_target(${stage}
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${directory}/${parent}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_OBJECTS:${module}>" "${directory}/${relative_path}"
            COMMAND_EXPAND_LISTS VERBATIM)
        add_dependencies(${stage} ${module})
        add_dependencies(${application} ${stage})
    endforeach()
endfunction()