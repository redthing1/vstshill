#pragma once

#include "../ext/args.hpp"
#include "command.hpp"

namespace vstk {

class GuiCommand : public Command {
public:
  explicit GuiCommand(args::Subparser& parser);
  ~GuiCommand() = default;

  int execute() override;
  const char* name() const override { return "gui"; }
  const char* description() const override {
    return "open plugin editor window";
  }

private:
  args::Subparser& parser_;
  args::Positional<std::string> plugin_path_;
  args::Flag audio_output_; // for future real-time audio output

  void open_plugin_gui(const std::string& plugin_path, bool with_audio) const;
};

} // namespace vstk