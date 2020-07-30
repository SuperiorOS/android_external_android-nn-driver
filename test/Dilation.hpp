//
// Copyright © 2017 Arm Ltd. All rights reserved.
// SPDX-License-Identifier: MIT
//

#pragma once

#include "DriverTestHelpers.hpp"

#include <armnn/LayerVisitorBase.hpp>

#include <boost/core/ignore_unused.hpp>
#include <boost/test/unit_test.hpp>

#include <numeric>

BOOST_AUTO_TEST_SUITE(DilationTests)

using namespace armnn;
using namespace boost;
using namespace driverTestHelpers;

struct DilationTestOptions
{
    DilationTestOptions() :
        m_IsDepthwiseConvolution{false},
        m_IsPaddingExplicit{false},
        m_HasDilation{false}
    {}

    ~DilationTestOptions() = default;

    bool m_IsDepthwiseConvolution;
    bool m_IsPaddingExplicit;
    bool m_HasDilation;
};

class DilationTestVisitor : public LayerVisitorBase<VisitorThrowingPolicy>
{
public:
    DilationTestVisitor() :
        DilationTestVisitor(1u, 1u)
    {}

    DilationTestVisitor(uint32_t expectedDilationX, uint32_t expectedDilationY) :
        m_ExpectedDilationX{expectedDilationX},
        m_ExpectedDilationY{expectedDilationY}
    {}

    void VisitConvolution2dLayer(const IConnectableLayer *layer,
                                 const Convolution2dDescriptor& descriptor,
                                 const ConstTensor& weights,
                                 const Optional<ConstTensor>& biases,
                                 const char *name = nullptr) override
    {
        ignore_unused(layer);
        ignore_unused(weights);
        ignore_unused(biases);
        ignore_unused(name);

        CheckDilationParams(descriptor);
    }

    void VisitDepthwiseConvolution2dLayer(const IConnectableLayer *layer,
                                          const DepthwiseConvolution2dDescriptor& descriptor,
                                          const ConstTensor& weights,
                                          const Optional<ConstTensor>& biases,
                                          const char *name = nullptr) override
    {
        ignore_unused(layer);
        ignore_unused(weights);
        ignore_unused(biases);
        ignore_unused(name);

        CheckDilationParams(descriptor);
    }

private:
    uint32_t m_ExpectedDilationX;
    uint32_t m_ExpectedDilationY;

    template<typename ConvolutionDescriptor>
    void CheckDilationParams(const ConvolutionDescriptor& descriptor)
    {
        BOOST_CHECK_EQUAL(descriptor.m_DilationX, m_ExpectedDilationX);
        BOOST_CHECK_EQUAL(descriptor.m_DilationY, m_ExpectedDilationY);
    }
};

template<typename HalPolicy>
void DilationTestImpl(const DilationTestOptions& options)
{
    using HalModel         = typename HalPolicy::Model;
    using HalOperationType = typename HalPolicy::OperationType;

    const armnn::Compute backend = armnn::Compute::CpuRef;
    auto driver = std::make_unique<ArmnnDriver>(DriverOptions(backend, false));
    HalModel model = {};

    // add operands
    std::vector<float> weightData(9, 1.0f);
    std::vector<float> biasData(1, 0.0f );

    // input
    AddInputOperand<HalPolicy>(model, hidl_vec<uint32_t>{1, 3, 3, 1});

    // weights & biases
    AddTensorOperand<HalPolicy>(model, hidl_vec<uint32_t>{1, 3, 3, 1}, weightData.data());
    AddTensorOperand<HalPolicy>(model, hidl_vec<uint32_t>{1}, biasData.data());

    uint32_t numInputs = 3u;
    // padding
    if (options.m_IsPaddingExplicit)
    {
        AddIntOperand<HalPolicy>(model, 1);
        AddIntOperand<HalPolicy>(model, 1);
        AddIntOperand<HalPolicy>(model, 1);
        AddIntOperand<HalPolicy>(model, 1);
        numInputs += 4;
    }
    else
    {
        AddIntOperand<HalPolicy>(model, android::nn::kPaddingSame);
        numInputs += 1;
    }

    AddIntOperand<HalPolicy>(model, 2); // stride x
    AddIntOperand<HalPolicy>(model, 2); // stride y
    numInputs += 2;

    if (options.m_IsDepthwiseConvolution)
    {
        AddIntOperand<HalPolicy>(model, 1); // depth multiplier
        numInputs++;
    }

    AddIntOperand<HalPolicy>(model, 0); // no activation
    numInputs += 1;

    // dilation
    if (options.m_HasDilation)
    {
        AddBoolOperand<HalPolicy>(model, false); // default data layout

        AddIntOperand<HalPolicy>(model, 2); // dilation X
        AddIntOperand<HalPolicy>(model, 2); // dilation Y

        numInputs += 3;
    }

    // output
    AddOutputOperand<HalPolicy>(model, hidl_vec<uint32_t>{1, 1, 1, 1});

    // set up the convolution operation
    model.operations.resize(1);
    model.operations[0].type = options.m_IsDepthwiseConvolution ?
        HalOperationType::DEPTHWISE_CONV_2D : HalOperationType::CONV_2D;

    std::vector<uint32_t> inputs(numInputs);
    std::iota(inputs.begin(), inputs.end(), 0u);
    std::vector<uint32_t> outputs = { numInputs };

    model.operations[0].inputs  = hidl_vec<uint32_t>(inputs);
    model.operations[0].outputs = hidl_vec<uint32_t>(outputs);

    // convert model
    ConversionData data({backend});
    data.m_Network = armnn::INetwork::Create();
    data.m_OutputSlotForOperand = std::vector<IOutputSlot*>(model.operands.size(), nullptr);

    bool ok = HalPolicy::ConvertOperation(model.operations[0], model, data);
    BOOST_CHECK(ok);

    // check if dilation params are as expected
    DilationTestVisitor visitor = options.m_HasDilation ? DilationTestVisitor(2, 2) : DilationTestVisitor();
    data.m_Network->Accept(visitor);
}

BOOST_AUTO_TEST_SUITE_END()
