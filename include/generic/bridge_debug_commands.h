#ifndef BRIDGE_DEBUG_COMMANDS_H_
#define BRIDGE_DEBUG_COMMANDS_H_

void interpret_debug_command(const uint8_t* src, uint8_t size, uint8_t real_size, bool is_in_command_loop);

#endif /* BRIDGE_DEBUG_COMMANDS_H_ */
