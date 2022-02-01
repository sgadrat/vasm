#include <fmt/core.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include <stdexcept>
#include <regex>

// g++ -std=c++20 `pkg-config --libs --cflags fmt` vasm.cpp -o vasm

typedef enum {
	A_ADD=0,
	A_ADC=1,
	A_SUB=2,
	A_SBC=3,
	A_CMP=4,
	A_NEG=6,
	A_XOR=8,
	A_LOAD=9,
	A_OR=10,
	A_AND=11,
	A_TEST=12,
	A_STORE=13
} AluOperation;

typedef struct {
	union {
		struct {
			uint16_t opB:3; // and the last three usually the second register (source register)
			uint16_t opN:3; // the next three can be anything
			uint16_t pad_hi0:10;
		};
		struct {
			uint16_t opimm:6; // the last six sometimes are a single immediate number
			uint16_t pad_hi1:10;
		};
		struct {
			uint16_t pad_lo:6;
			uint16_t op1:3; // and the next three the addressing mode
			uint16_t opA:3; // the next three are usually the destination register
			uint16_t op0:4; // the top four bits are the alu op or the branch condition, or E or F
		};
		uint16_t raw;
	};
} Instruction;

typedef enum {
	R_SP,
	R_R1,
	R_R2,
	R_R3,
	R_R4,
	R_BP,
	R_SR,
	R_PC
} Register;

#if 0
static void asm_set(Register r, uint16_t value) {
	cursor->op1 = 4;
	cursor->opN = 1;
	cursor->opA = r;
	cursor->opB = r;
	cursor->op0 = A_LOAD; // x = r[opB]
	cursor++;
	cursor->raw = value;
	cursor++;
}
#endif

static uint16_t asm_load(Instruction* cursor, Register r, uint16_t address) {
	cursor->op1 = 4; // [imm16]
	cursor->opN = 2;
	cursor->opA = r;
	cursor->opB = r;
	cursor->op0 = A_LOAD; // x = r[opB]
	cursor++;
	cursor->raw = address;
	return 2;
}

#if 0
static void asm_store(uint16_t address, Register r) {
	cursor->op1 = 4;
	cursor->opN = 3;
	cursor->opA = r;
	cursor->opB = r;
	cursor->op0 = A_STORE; // x = r[opB]
	cursor++;
	cursor->raw = address;
	cursor++;
}

typedef enum {
	C_NEQUAL=4,
	C_EQUAL=5,
	C_ALWAYS=14
} Condition;

static void asm_alu(AluOperation op, Register ra, Register rb) {
	cursor->op1 = 4;
	cursor->opN = 0;
	cursor->op0 = op;
	cursor->opA = ra;
	cursor->opB = rb;
	cursor++;
}


static void asm_jump(Condition c, Instruction* target) {
	uint16_t pc = (cursor+1) - code;
	uint16_t address = target - code;

	cursor->op0 = c;
	cursor->opA = 7;
	cursor->op1 = (address < pc) ? 1 : 0; // 1 = backwards, 0 = forwards
	cursor->opimm = abs(address - pc);
	cursor++;
}

static void asm_goto(Instruction* target) {
	uint32_t address = target - code;
	cursor->op0 = 15;
	cursor->op1 = 2; // JMPF
	cursor->opA = 7;
	cursor->opimm = address >> 16;
	cursor++;
	cursor->raw = address & 0xFFFF;
	cursor++;
}
#endif

struct UnresolvedSymbolValue {
	std::string symbol;
	Instruction* value_position;
	size_t line;
};

static std::string lower(std::string val) {
	for (auto char_it = val.begin(); char_it != val.end(); ++char_it) {
		if (*char_it >= 'A' && *char_it <= 'Z') {
			*char_it = 'a' + (*char_it) - 'A';
		}
	}
	return val;
}

class ParseError : public std::runtime_error {
	public:
	ParseError(std::string msg): std::runtime_error(msg) {}
};

/**
 * Return the value of an address parameter.
 *
 * If the address is valid but cannot be determined, returns 0 and mark its position for resolution during the second pass.
 */
static uint16_t get_addr(
	std::string value,
	std::map<std::string, uint16_t> const& symbols,
	std::vector<UnresolvedSymbolValue>& unresolved_symbols,
	Instruction* value_position,
	size_t line_num
)
{
	if (value.empty()) {
		throw ParseError("invalid value <empty string>");
	}

	try {
		if (value[0] == '$') {
			// Hexadecimal value
			return std::stoul(std::string(value, 1), nullptr, 16);
		}else if (value[0] == '%') {
			// Binary value
			return std::stoul(std::string(value, 1), nullptr, 2);
		}else if (value[0] >= '0' and value[0] <= '9') {
			// Decimal value
			return std::stoul(value, nullptr, 10);
		}else {
			// Symbol name
			auto symbol_it = symbols.find(value);
			if (symbol_it != symbols.end()) {
				// Known symbol, use its value
				return symbol_it->second;
			}else {
				// Unknown symbol, store for resolving later
				unresolved_symbols.push_back({
					.symbol = value,
					.value_position = value_position,
					.line = line_num
				});
			}
		}
	}catch (std::invalid_argument const& e) {
		throw ParseError("invalid number format");
	}

	return 0;
}

/**
 * Return the value of a parameter.
 *
 * Known labels can be used in resolution, but unknowns will trigger an error.
 */
static uint16_t get_value(
	std::string value,
	std::map<std::string, uint16_t> const& symbols
)
{
	if (value.empty()) {
		throw ParseError("invalid value <empty string>");
	}

	try {
		if (value[0] == '$') {
			// Hexadecimal value
			return std::stoul(std::string(value, 1), nullptr, 16);
		}else if (value[0] == '%') {
			// Binary value
			return std::stoul(std::string(value, 1), nullptr, 2);
		}else if (value[0] >= '0' and value[0] <= '9') {
			// Decimal value
			return std::stoul(value, nullptr, 10);
		}else {
			// Symbol name
			auto symbol_it = symbols.find(value);
			if (symbol_it != symbols.end()) {
				// Known symbol, use its value
				return symbol_it->second;
			}else {
				// Unknown symbol
				throw ParseError(fmt::format("unresolved symbol '{}'", value));
			}
		}
	}catch (std::invalid_argument const& e) {
		throw ParseError("invalid number format");
	}
}

static Register parse_register_name(std::string name) {
	name = lower(name);
	if (name == "sp") {
		return R_SP;
	}
	if (name == "r1") {
		return R_R1;
	}
	if (name == "r2") {
		return R_R2;
	}
	if (name == "r3") {
		return R_R3;
	}
	if (name == "r4") {
		return R_R4;
	}
	if (name == "bp") {
		return R_BP;
	}
	if (name == "sr") {
		return R_SR;
	}
	if (name == "pc") {
		return R_PC;
	}

	throw ParseError("unknown register name");
}

size_t g_error_count;

void error(size_t line_num, std::string message) {
	std::cerr << "error: " << line_num << ": " << message << '\n';
	++g_error_count;
	if (g_error_count >= 20) {
		throw std::runtime_error("stopping after 20 errors");
	}
}

int main(int argc, char** argv) {
	try {
		// Read source file
		std::ifstream src_file(argv[1]);
		if (!src_file) {
			std::cerr << "unable to open file '" << argv[1] << "'\n";
			return 1;
		}

		src_file.seekg (0, src_file.end);
		size_t length = src_file.tellg();
		src_file.seekg (0, src_file.beg);

		std::string src(length, '\0');
		src_file.read(src.data(), length);
		if (!src_file) {
			std::cerr << "failed to read file '" << argv[1] << "'\n";
		}
		src_file.close();

		// First pass: read the file, generate the code with placeholders where necessary
		g_error_count = 0;

		uint16_t mem[0x20000];
		memset(mem, 0x00, sizeof(mem));
		Instruction* cursor = reinterpret_cast<Instruction*>(mem);
		uint16_t pc = 0;

		std::map<std::string, uint16_t> symbols;
		std::vector<UnresolvedSymbolValue> unresolved_symbols;

		size_t line_num = 0;
		std::string::const_iterator src_cursor = src.begin();
		while (src_cursor != src.end()) {
			// Get the current line
			++line_num;
			std::string line;
			char c;
			do {
				c = *src_cursor;
				line += c;
				++src_cursor;
			}while (c != '\n' && src_cursor != src.end());

			if (c == '\n') {
				line.resize(line.size() - 1);
			}

			// Parse the line
			//  "label:instruction param1, param2, param3;comment" (with possible extra whitespaces)
			std::regex line_regex("( |\\t)*([a-zA-Z0-9_]+:)?( |\\t)*([a-zA-Z.]+)?( |\\t)*([a-zA-Z0-9_$\%, ]+)?(;.*)?");
			std::smatch matches;
			if (!std::regex_match(line, matches, line_regex)) {
				error(line_num, "unable to interpret line");
			}

			std::string label = matches[2];
			std::string instruction = matches[4];
			std::string str_parameters = matches[6];
			std::vector<std::string> parameters;
			//std::cerr << "dbg: " << line_num << ": label=" << label << " op=" << instruction << " params='" << str_parameters << "'\n";

			if (!label.empty()) {
				// Remove the final ':'
				label.resize(label.size() - 1);
			}

			instruction = lower(instruction);

			std::string current_parameter;
			for (std::string::const_iterator param_it = str_parameters.begin(); param_it != str_parameters.end(); ++param_it) {
				if (*param_it == ',') {
					parameters.push_back(current_parameter);
					current_parameter = "";
				}else if (*param_it != ' ') {
					current_parameter += *param_it;
				}
			}
			if (!current_parameter.empty()) {
				parameters.push_back(current_parameter);
			}

			// Store label address
			if (!label.empty()) {
				if (symbols.insert(std::pair<std::string, uint16_t>(label, pc)).second == false) {
					error(line_num, fmt::format("redefining label '{}'", label));
				}
			}

			// Generate instruction code
			if (instruction.empty()) {
			}else if (instruction == ".word") {
				if (parameters.size() < 1) {
					error(line_num, ".WORD requires at least 1 parameter (value)");
				}else {
					for (auto str_value: parameters) {
						uint16_t value = 0;
						try {
							value = get_addr(str_value, symbols, unresolved_symbols, cursor, line_num);
						}catch (ParseError const& e) {
							error(line_num, fmt::format(".WORD unable to parse value '{}': {}", str_value, e.what()));
						}
						cursor->raw = value;
						++cursor;
						++pc;
					}
				}
			}else if (instruction == ".dsb") {
				if (parameters.size() != 2) {
					error(line_num, ".DSB requires 2 parameters (value, count)");
				}else {
					uint16_t value = 0;
					uint16_t count = 0;
					try {
						value = get_value(parameters[0], symbols);
					}catch (ParseError const& e) {
						error(line_num, fmt::format(".DSB unable to parse value '{}': {}", parameters[0], e.what()));
					}
					try {
						count = get_value(parameters[1], symbols);
					}catch (ParseError const& e) {
						error(line_num, fmt::format(".DSB unable to parse count '{}': {}", parameters[1], e.what()));
					}

					while (count > 0) {
						cursor->raw = value;
						++cursor;
						++pc;
						--count;
					}
				}
			}else if (instruction == ".label") {
				if (parameters.size() != 2) {
					error(line_num, ".LABEL requires 2 parameters (name, value)");
				}else {
					std::string label = parameters[0];
					uint16_t value = 0;
					try {
						value = get_value(parameters[1], symbols);
					}catch (ParseError const& e) {
						error(line_num, fmt::format(".LABEL unable to parse value '{}': {}", parameters[1], e.what()));
					}

					if (symbols.insert(std::pair<std::string, uint16_t>(label, value)).second == false) {
						error(line_num, fmt::format("redefining label '{}'", label));
					}
				}
			}else if (instruction == "load") {
				if (parameters.size() != 2) {
					error(line_num, "LOAD requires 2 parameters (register, address)");
				}else {
					Register reg;
					uint16_t addr;
					bool has_error = false;
					try {
						reg = parse_register_name(parameters[0]);
					}catch(ParseError const& e) {
						error(line_num, fmt::format("LOAD first parameters '{}' is not a valid register name", parameters[0]));
						has_error = true;
					}
					try {
						addr = get_addr(parameters[1], symbols, unresolved_symbols, cursor+1, line_num);
					}catch(ParseError const& e) {
						error(line_num, fmt::format("LOAD second parameters '{}' is not a valid address", parameters[1]));
						has_error = true;
					}

					if (! has_error) {
						uint16_t size = asm_load(cursor, reg, addr);
						cursor += size;
						pc += size;
					}
				}
			}else {
				error(line_num, fmt::format("unknown instruction '{}'", instruction));
			}
		}

		// Second pass: fill placeholders
		for (UnresolvedSymbolValue const& info: unresolved_symbols) {
			std::map<std::string, uint16_t>::const_iterator symbol_it = symbols.find(info.symbol);
			if (symbol_it == symbols.end()) {
				error(info.line, fmt::format("unresolved symbol '{}'", info.symbol));
			}else {
				info.value_position->raw = symbol_it->second;
			}
		}

		// Write result
		FILE* f = fopen("rom.bin", "wb");
		fwrite(mem, sizeof(mem), 1, f);
		fclose(f);
	}catch (std::runtime_error const& e) {
		std::cerr << "premature ending: " << e.what() << '\n';
		return 1;
	}

	return 0;
}
