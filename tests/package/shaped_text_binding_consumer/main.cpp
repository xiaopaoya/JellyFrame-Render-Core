#include <render_core/shaped_text_binding.h>

int main() {
  jellyframe::shaped_text::Binding binding;
  if (binding.valid() || binding.resource_id() || binding.generation()) return 1;
  jellyframe::shaped_text::Inputs inputs;
  jellyframe::shaped_text::Binding output;
  jellyframe::shaped_text::Limits limits;
  std::uint64_t work = 1000;
  std::string packet = "retained";
  jellyframe::shaped_text::Metrics metrics;
  const auto create_result = jellyframe::shaped_text::create_binding(
      inputs, limits, work, output);
  const auto result = jellyframe::shaped_text::prepare(
      binding, 16000, {}, {}, work, packet, metrics);
  const auto validate_result = jellyframe::shaped_text::validate_packet(
      binding, {}, limits, work);
  return create_result == jellyframe::shaped_text::Status::invalid &&
                 result == jellyframe::shaped_text::Status::invalid &&
                 validate_result == jellyframe::shaped_text::Status::invalid &&
                 packet == "retained"
             ? 0
             : 2;
}
