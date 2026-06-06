#pragma once 
#include <unordered_map>
#include <cstdint>
#include <string>

namespace quark::sm::attrs{
    enum class AttributeTarget : uint32_t {
        None      = 0,
    	Function  = 1 << 0,
    	Variable  = 1 << 1,
		Field 	  = 1 << 2,
   		Struct    = 1 << 3,
    	Module    = 1 << 4,
	};
	inline AttributeTarget operator|(AttributeTarget a, AttributeTarget b) {
    	return static_cast<AttributeTarget>(
        	static_cast<uint32_t>(a) |
        	static_cast<uint32_t>(b)
    	);
	}
	struct AttributeInfo {
    	std::string name;
    	AttributeTarget targets;
    	uint32_t min_args;
    	uint32_t max_args;
	};
	inline std::unordered_map<std::string, AttributeInfo> attributes = {
    	{ "entry", { "entry", AttributeTarget::Function, 0, 0 }},
		{ "init",  { "init", AttributeTarget::Variable, 0, 0 }},
		{ "guard", { "guard", AttributeTarget::Variable, 1, 1 }},
		{ "public", { "public", AttributeTarget::Function | AttributeTarget::Variable 
                        | AttributeTarget::Field | AttributeTarget::Struct, 0, 0}},
		{ "private", { "private", AttributeTarget::Function | AttributeTarget::Variable
                         | AttributeTarget::Field | AttributeTarget::Struct, 0, 0}},
		{ "hide", {"hide", AttributeTarget::Module, 0, 0}}
	};
}
