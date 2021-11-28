#include "mutator.h"
#include <fstream>
#include <iostream>
#include <unistd.h>

#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/reflection.h"

#include "libprotobuf-mutator/src/libfuzzer/libfuzzer_macro.h"

#include "gen/out.pb.h"
#include "libprotobuf-mutator/src/mutator.h"

#include <algorithm>
#include <random>
#include <cstdlib>


void ProtoToDataHelper(std::stringstream &out, const google::protobuf::Message &msg) {
  const google::protobuf::Descriptor *desc = msg.GetDescriptor();
  const google::protobuf::Reflection *refl = msg.GetReflection();

  const unsigned fields = desc->field_count();
  // std::cout << msg.DebugString() << std::endl;
  for (unsigned i = 0; i < fields; ++i) {
    const google::protobuf::FieldDescriptor *field = desc->field(i);

    // 对于单个 choice
    if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      // 如果当前是 choice list
      if (field->is_repeated()) {
        const google::protobuf::RepeatedFieldRef<google::protobuf::Message> &ptr = refl->GetRepeatedFieldRef<google::protobuf::Message>(msg, field);
        // 将每个 choice 打出来
        for (const auto &child : ptr) {
          ProtoToDataHelper(out, child);
          out << "\n";
        }
      // 如果当前是某个子 choice
      } else if (refl->HasField(msg, field)) {
        const google::protobuf::Message &child = refl->GetMessage(msg, field);
        ProtoToDataHelper(out, child);
      }
    } 
    // 对于单个 field
    else if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_INT32) {
      out << refl->GetInt32(msg, field);
      if(i < fields - 1) 
        out << " ";
    } 
    else if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_STRING) {
      out << refl->GetString(msg, field);
      if(i < fields - 1) 
        out << " ";
    } 
    else {
      abort();
    }

  }
}

// Apparently the textual generator kinda breaks?
DEFINE_BINARY_PROTO_FUZZER(const menuctf::ChoiceList &root) {
  std::stringstream stream;
  ProtoToDataHelper(stream, root);
  std::string data = stream.str();
  std::replace(data.begin(), data.end(), '\n', '|');
  puts(("ProtoToDataHelper: " + data).c_str());
  puts("====================================================================================================");
  // std::string dbg = root.DebugString();
  // std::replace(dbg.begin(), dbg.end(), '\n', ' ');
}

// AFLPlusPlus interface
extern "C" {
  void *afl_custom_init(void *afl, unsigned int seed) {
    #pragma unused (afl)

    auto mutator = new protobuf_mutator::Mutator();
    mutator->Seed(seed);
    return mutator;
  }
  
  void afl_custom_deinit(void *data) {
    protobuf_mutator::Mutator *mutator = (protobuf_mutator::Mutator*)data;
    delete mutator;
  }
  
  // afl_custom_fuzz
  size_t afl_custom_fuzz(void *data, unsigned char *buf, size_t buf_size, unsigned char **out_buf, 
                         unsigned char *add_buf, size_t add_buf_size, size_t max_size) {
    #pragma unused (add_buf)
    #pragma unused (add_buf_size)
    
    static uint8_t *saved_buf = nullptr;

    assert(buf_size <= max_size);
    
    uint8_t *new_buf = (uint8_t *) realloc((void *)saved_buf, max_size);
    if (!new_buf) 
        abort();
    saved_buf = new_buf;

    protobuf_mutator::Mutator *mutator = (protobuf_mutator::Mutator*)data;

    menuctf::ChoiceList msg;
    if (!protobuf_mutator::libfuzzer::LoadProtoInput(true, buf, buf_size, &msg))
      abort();

    // 弃用合并两个 Message 的 CrossOver 函数，因为它的变异效果有亿点点拉胯
    mutator->Mutate(&msg, max_size);
    
    std::string out_str;
    msg.SerializePartialToString(&out_str);

    memcpy(new_buf, out_str.c_str(), out_str.size());
    *out_buf = new_buf;
    return out_str.size();
  }

  size_t afl_custom_post_process(void* data, uint8_t *buf, size_t buf_size, uint8_t **out_buf) {
    #pragma unused (data)
    // new_data is never free'd by pre_save_handler
    // I prefer a slow but clearer implementation for now
    
    static uint8_t *saved_buf = NULL;

    menuctf::ChoiceList msg;
    std::stringstream stream;
    // 如果加载成功
    if (protobuf_mutator::libfuzzer::LoadProtoInput(true, buf, buf_size, &msg))
      ProtoToDataHelper(stream, msg);
    else
      // 必须保证成功
      abort();
    const std::string str = stream.str();

    uint8_t *new_buf = (uint8_t *) realloc((void *)saved_buf, str.size());
    if (!new_buf) {
      *out_buf = buf;
      return buf_size;
    }
    *out_buf = saved_buf = new_buf;

    memcpy((void *)new_buf, str.c_str(), str.size());

    return str.size();
  }
}