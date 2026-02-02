unsigned char Error_gem[] =
"//Deprecate, this file is no longer used by the vm.\n"
"\n"
"class Error{\n"
"    init(msg){\n"
"        this.msg = msg;\n"
"        this.stackTrace = nil;\n"
"    }\n"
"    toString(){\n"
"        return this.msg + \"\\n\" + this.stackTrace;\n"
"    }\n"
"}\n"
"\n"
"class IndexOutOfBoundsError :: Error{}\n"
"class TypeError :: Error{}\n"
"class NameError :: Error{}\n"
"class AccessError :: Error{}\n"
"class IllegalArgumentError :: Error{}\n"
"class LookUpError :: Error{}\n"
"class FormatError :: Error{}\n"
"\n";

unsigned int Error_gem_len = sizeof(Error_gem) - 1;
