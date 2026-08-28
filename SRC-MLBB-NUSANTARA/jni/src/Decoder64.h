#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <GLES3/gl3.h>
#include "imgui.h"
#include "icon/HeroIcons.h"


inline std::vector<unsigned char> DecodeBase64(const std::string& input) {
    static const std::string b64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[b64_chars[i]] = i;
    std::vector<unsigned char> out;
    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}


static std::unordered_map<int, ImTextureID> g_TextureCache;

inline ImTextureID GetHeroTexture(int heroId) {
    
    if (g_TextureCache.count(heroId)) {
        return g_TextureCache[heroId];
    }

    
    auto it = g_HeroIconsBase64.find(heroId);
    if (it != g_HeroIconsBase64.end()) {
        std::vector<unsigned char> data = DecodeBase64(it->second);
        int w, h, ch;
        
        
        
        
        unsigned char* pixels = stbi_load_from_memory(data.data(), data.size(), &w, &h, &ch, 4);
        
        if (pixels) {
            GLuint tex;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            stbi_image_free(pixels);
            
            ImTextureID res = (ImTextureID)(intptr_t)tex;
            g_TextureCache[heroId] = res;
            return res;
        }
    }
    
    
    return nullptr;
}


inline void ClearHeroTextureCache() {
    for (auto& pair : g_TextureCache) {
        GLuint tex = (GLuint)(intptr_t)pair.second;
        glDeleteTextures(1, &tex);
    }
    g_TextureCache.clear();
}
