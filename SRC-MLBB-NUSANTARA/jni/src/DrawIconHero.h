#pragma once
#include "imgui.h"
#include "Decoder64.h"
#include <cmath>

inline void DrawHeroIcon(ImDrawList* drawList, ImVec2 pos, int heroId, int hp, int maxHp, float radius = 50.0f) {
    ImTextureID tex = GetHeroTexture(heroId);
    
    
    drawList->AddCircleFilled(pos, radius + 2.0f, IM_COL32(0, 0, 0, 150), 40);

    
    if (tex) {
        drawList->AddImageRounded(tex, 
            ImVec2(pos.x - radius, pos.y - radius), 
            ImVec2(pos.x + radius, pos.y + radius), 
            ImVec2(0,0), ImVec2(1,1), 
            IM_COL32(255,255,255,255), radius);
    } else {
        
        drawList->AddCircleFilled(pos, radius, IM_COL32(100, 100, 100, 255), 40);
    }

    
    if (maxHp > 0) {
        float healthPercent = (float)hp;
        if (healthPercent > 1.0f) healthPercent = 1.0f;
        if (healthPercent < 0.0f) healthPercent = 0.0f;
        
        float startAngle = -1.5708f; 
        float endAngle = startAngle + (healthPercent * 6.28318f); 
        
        
        ImU32 hpColor = (healthPercent > 0.4f) ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 0, 0, 255);
        
        
        drawList->PathArcTo(pos, radius + 1.0f, -1.5708f, 4.7124f, 40);
        drawList->PathStroke(IM_COL32(0, 0, 0, 120), false, 3.0f);
        
        
        drawList->PathArcTo(pos, radius + 1.0f, startAngle, endAngle, 40);
        drawList->PathStroke(hpColor, false, 2.5f);
    }
}
