// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/graph/initializer.h"
#include "core/graph/conv_activation_fusion.h"
#include "core/graph/graph_utils.h"
#include <deque>

using namespace onnx;
using namespace ::onnxruntime::common;
namespace onnxruntime {

namespace {
bool IsFusableActivation(const Node& node) {
  return utils::IsSupportedOptypeVersionAndDomain(node, "LeakyRelu", 6) || utils::IsSupportedOptypeVersionAndDomain(node, "Relu", 6) || utils::IsSupportedOptypeVersionAndDomain(node, "Sigmoid", 6) || utils::IsSupportedOptypeVersionAndDomain(node, "Tanh", 6);
}

void HandleActivationNodeEdges(Graph& g, const Node& act, Node& fused_conv) {
  Node::EdgeSet output_edges;
  for (auto it = act.OutputEdgesBegin(); it != act.OutputEdgesEnd(); ++it) {
    output_edges.insert(*it);
  }

  //remove output edge of activation
  //connect fused_conv node and nodes after activation nodes
  for (auto& output_edge : output_edges) {
    NodeIndex dst_node_index = output_edge.GetNode().Index();
    int src_arg_index = output_edge.GetSrcArgIndex();
    int dst_arg_index = output_edge.GetDstArgIndex();
    g.RemoveEdge(act.Index(), dst_node_index, src_arg_index, dst_arg_index);
    g.AddEdge(fused_conv.Index(), dst_node_index, 0, dst_arg_index);
  }
}

}  // namespace

Status ConvActivationFusion::Apply(Graph& graph, bool& modified) const {
  GraphViewer graph_viewer(graph);
  const auto& order = graph_viewer.GetNodesInTopologicalOrder();

  std::deque<onnxruntime::NodeIndex> removed_nodes;
  for (auto index : order) {
    auto node = graph.GetNode(index);
    if (!utils::IsSupportedOptypeVersionAndDomain(*node, "Conv", 1) || node->GetOutputEdgesCount() != 1) {
      continue;
    }
    const Node& next_node = *(node->OutputNodesBegin());
    if (!IsFusableActivation(next_node) || graph.IsNodeOutputsInGraphOutputs(next_node)) {
      continue;
    }

    Node* conv_node = node;
    const Node& act_node = next_node;

    Node& fused_conv = graph.AddNode(graph.GenerateNodeName("fused " + conv_node->Name()), "FusedConv",
                                     "fused Conv " + conv_node->Name() + "with activation " + act_node.OpType(),
                                     conv_node->MutableInputDefs(),
                                     conv_node->MutableOutputDefs(),
                                     &conv_node->GetAttributes(),
                                     "com.microsoft");

    //Add a new attribute to specify the activation type
    fused_conv.AddAttribute("activation", act_node.OpType());

    //Add optional attributes for activations
    if (act_node.OpType() == "LeakyRelu") {
      const NodeAttributes& attrs = act_node.GetAttributes();
      for (const auto& attr : attrs) {
        fused_conv.AddAttribute(attr.first, attr.second);
      }
    }

    HandleActivationNodeEdges(graph, act_node, fused_conv);

    // Replace the input of the node following activation node
    const NodeArg* act_output_def = act_node.OutputDefs()[0];
    NodeArg* fused_conv_output_def = fused_conv.MutableOutputDefs()[0];
    for (auto it = act_node.OutputNodesBegin(); it != act_node.OutputNodesEnd(); ++it) {
      auto output_node = graph.GetNode((*it).Index());
      if (!output_node) {
        return Status(ONNXRUNTIME, INVALID_ARGUMENT);
      }

      auto& input_defs = output_node->MutableInputDefs();
      for (auto& def : input_defs) {
        if (def == act_output_def) {
          def = fused_conv_output_def;
        }
      }
    }

    removed_nodes.push_front(conv_node->Index());
    removed_nodes.push_front(act_node.Index());
  }

  for (auto node : removed_nodes) {
    graph.RemoveNode(node);
  }

  if (!removed_nodes.empty()) {
    modified = true;
    ORT_RETURN_IF_ERROR(graph.Resolve());
  }
  return Status::OK();
}
}  // namespace onnxruntime
