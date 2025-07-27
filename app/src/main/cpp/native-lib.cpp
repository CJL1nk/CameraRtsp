#include <jni.h>
#include <map>
#include <string>

#define SPROP_VPS "sprop-vps"
#define SPROP_SPS "sprop-sps"
#define SPROP_PPS "sprop-pps"

#define NAL_HEADER_SIZE 2
#define NAL_TYPE(data, start) ((data[start] >> 1) & 0x3F)

static const char base64Table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

std::string base64EncodeNal(const uint8_t* data, size_t offset, size_t limit) {
    std::string result;
    result.reserve(((limit - offset + 2) / 3) * 4);

    size_t i = offset;
    while (i + 2 < limit) {
        uint32_t triple =
                (data[offset + i] << 16) |
                (data[offset + i + 1] << 8) |
                data[offset + i + 2];

        result.push_back(base64Table[(triple >> 18) & 0x3F]);
        result.push_back(base64Table[(triple >> 12) & 0x3F]);
        result.push_back(base64Table[(triple >> 6) & 0x3F]);
        result.push_back(base64Table[triple & 0x3F]);
        i += 3;
    }

    if (i < limit) {
        uint32_t triple = data[offset + i] << 16;
        if (i + 1 < limit) {
            triple |= data[offset + i + 1] << 8;
        }

        result.push_back(base64Table[(triple >> 18) & 0x3F]);
        result.push_back(base64Table[(triple >> 12) & 0x3F]);
        if (i + 1 < limit) {
            result.push_back(base64Table[(triple >> 6) & 0x3F]);
        } else {
            result.push_back('=');
        }
        result.push_back('=');
    }

    return result;
}

int findNalStart(const uint8_t* data, int start, int limit) {
    for (int i = start; i < limit - 3; i++) {
        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
            return i + 3;
        }
        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
            return i + 4;
        }
    }
    return -1;
}

std::vector<std::pair<int, int>> extractNalUnitRanges(const uint8_t* data, int start, int limit) {
    std::vector<std::pair<int, int>> nalRanges;
    int offset = start;

    while (offset < limit) {
        int nalStart = findNalStart(data, offset, limit);
        if (nalStart < 0) break;

        int next = findNalStart(data, nalStart, limit);
        int end = (next > 0) ? next : limit;

        nalRanges.emplace_back(start, end);
        offset = end;
    }

    return nalRanges;
}



extern "C"
JNIEXPORT jobject JNICALL
Java_com_pntt3011_cameraserver_server_packetizer_H265Packetizer_extractH265ParamSets(JNIEnv *env,
                                                                                     jobject thiz,
                                                                                     jobject buffer,
                                                                                     jint position,
                                                                                     jint limit) {
    auto* data  = reinterpret_cast<uint8_t*>(env->GetDirectBufferAddress(buffer));
    if (data  == nullptr) {
        return nullptr;
    }
    std::map<std::string, std::string> nalMap;
    auto nalRanges = extractNalUnitRanges(data, position, limit);

    for (const auto& range : nalRanges) {
        if (range.second - range.first >= 2) {
            int type = NAL_TYPE(data, range.first);
            std::string b64 = base64EncodeNal(data, range.first, range.second);
            if (type == 32)
                nalMap[SPROP_VPS] = b64;
            else if (type == 33)
                nalMap[SPROP_SPS] = b64;
            else if (type == 34)
                nalMap[SPROP_PPS] = b64;
        }
    }

    // Convert map to Java HashMap<String, String>
    jclass hashMapClass = env->FindClass("java/util/HashMap");
    jmethodID hashMapInit = env->GetMethodID(hashMapClass, "<init>", "()V");
    jmethodID putMethod = env->GetMethodID(hashMapClass, "put",
                                           "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");

    jobject hashMap = env->NewObject(hashMapClass, hashMapInit);

    for (const auto& [key, val] : nalMap) {
        jstring jKey = env->NewStringUTF(key.c_str());
        jstring jVal = env->NewStringUTF(val.c_str());
        env->CallObjectMethod(hashMap, putMethod, jKey, jVal);
        env->DeleteLocalRef(jKey);
        env->DeleteLocalRef(jVal);
    }
    return hashMap;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_pntt3011_cameraserver_server_packetizer_H265Packetizer_extractH265Packets(JNIEnv *env,
                                                                                   jobject thiz,
                                                                                   jobject buffer,
                                                                                   jint position,
                                                                                   jint limit,
                                                                                   jintArray size_array,
                                                                                   jobjectArray data_array) {
    auto* data  = reinterpret_cast<uint8_t*>(env->GetDirectBufferAddress(buffer));
    if (data == nullptr) {
        return -1;
    }

    auto nalRanges = extractNalUnitRanges(data, position, limit);

}