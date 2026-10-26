#include "pipeline.hpp"

#include <renderer/optix_backend/error.hpp>

#include <cuda_driver_wrapper/context.hpp>

#include <optix_stubs.h>
#include <optix_stack_size.h>

#include <array>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <utility>

namespace renderer::triangle::detail
{

Pipeline::Pipeline(
    const optix_backend::DeviceContext& context, std::span<const char> module_code)
    : cuda_context_(context.cuda_context())
{
    if (module_code.empty())
    {
        throw std::invalid_argument("The triangle OptiX module is empty");
    }

    cuda_driver_wrapper::ScopedCurrentContext current(cuda_context_);
    try
    {
        constexpr unsigned int max_trace_depth = 1;
        OptixPipelineCompileOptions compile_options{};
        compile_options.usesMotionBlur = false;
        compile_options.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_GAS;
        compile_options.numPayloadValues = 3;
        compile_options.numAttributeValues = 2;
        compile_options.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
        compile_options.pipelineLaunchParamsVariableName = "params";
        compile_options.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;

        OptixModuleCompileOptions module_options{};
        module_options.optLevel = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
        module_options.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_MINIMAL;

        std::array<char, 8192> log{};
        std::size_t log_size = log.size();
        const OptixResult module_result = optixModuleCreate(
            context.handle(), &module_options, &compile_options,
            module_code.data(), module_code.size(), log.data(), &log_size, &module_);
        optix_backend::check_optix_log(module_result, "optixModuleCreate", log, log_size);

        const OptixProgramGroupOptions group_options{};
        const auto create_group = [&](const OptixProgramGroupDesc& description,
                                      OptixProgramGroup& group,
                                      const char* operation)
        {
            log.fill('\0');
            log_size = log.size();
            const OptixResult result = optixProgramGroupCreate(
                context.handle(), &description, 1, &group_options,
                log.data(), &log_size, &group);
            optix_backend::check_optix_log(result, operation, log, log_size);
        };

        OptixProgramGroupDesc raygen_description{};
        raygen_description.kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
        raygen_description.raygen.module = module_;
        raygen_description.raygen.entryFunctionName = "__raygen__triangle";
        create_group(raygen_description, raygen_, "optixProgramGroupCreate(raygen)");

        OptixProgramGroupDesc miss_description{};
        miss_description.kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
        miss_description.miss.module = module_;
        miss_description.miss.entryFunctionName = "__miss__background";
        create_group(miss_description, miss_, "optixProgramGroupCreate(miss)");

        OptixProgramGroupDesc hitgroup_description{};
        hitgroup_description.kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
        hitgroup_description.hitgroup.moduleCH = module_;
        hitgroup_description.hitgroup.entryFunctionNameCH = "__closesthit__triangle";
        create_group(hitgroup_description, hitgroup_, "optixProgramGroupCreate(hitgroup)");

        const std::array groups{raygen_, miss_, hitgroup_};
        OptixPipelineLinkOptions link_options{};
        link_options.maxTraceDepth = max_trace_depth;
        log.fill('\0');
        log_size = log.size();
        const OptixResult pipeline_result = optixPipelineCreate(
            context.handle(), &compile_options, &link_options, groups.data(),
            static_cast<unsigned int>(groups.size()), log.data(), &log_size, &pipeline_);
        optix_backend::check_optix_log(pipeline_result, "optixPipelineCreate", log, log_size);

        OptixStackSizes stack_sizes{};
        for (OptixProgramGroup group : groups)
        {
            optix_backend::check_optix(
                optixUtilAccumulateStackSizes(group, &stack_sizes, pipeline_),
                "optixUtilAccumulateStackSizes");
        }
        unsigned int direct_from_traversal = 0;
        unsigned int direct_from_state = 0;
        unsigned int continuation = 0;
        optix_backend::check_optix(optixUtilComputeStackSizes(
            &stack_sizes, max_trace_depth, 0, 0,
            &direct_from_traversal, &direct_from_state, &continuation),
            "optixUtilComputeStackSizes");
        optix_backend::check_optix(optixPipelineSetStackSize(
            pipeline_, direct_from_traversal, direct_from_state, continuation, 1),
            "optixPipelineSetStackSize");
        current.restore();
    }
    catch (...)
    {
        close_noexcept();
        throw;
    }
}

Pipeline::~Pipeline() noexcept
{
    close_noexcept();
}

void Pipeline::close_noexcept() noexcept
{
    if (pipeline_ == nullptr && raygen_ == nullptr && miss_ == nullptr
        && hitgroup_ == nullptr && module_ == nullptr)
    {
        return;
    }
    try
    {
        cuda_driver_wrapper::ScopedCurrentContext current(cuda_context_);
        if (pipeline_ != nullptr)
        {
            optix_backend::report_optix_cleanup(
                optixPipelineDestroy(std::exchange(pipeline_, nullptr)), "optixPipelineDestroy");
        }
        for (OptixProgramGroup* group : {&hitgroup_, &miss_, &raygen_})
        {
            if (*group != nullptr)
            {
                optix_backend::report_optix_cleanup(
                    optixProgramGroupDestroy(std::exchange(*group, nullptr)),
                    "optixProgramGroupDestroy");
            }
        }
        if (module_ != nullptr)
        {
            optix_backend::report_optix_cleanup(
                optixModuleDestroy(std::exchange(module_, nullptr)), "optixModuleDestroy");
        }
        current.restore();
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "[triangle pipeline cleanup] %s\n", error.what());
    }
    catch (...)
    {
        std::fputs("[triangle pipeline cleanup] unknown failure\n", stderr);
    }
}

} // namespace renderer::triangle::detail
