#pragma once

#include "../common.hlsli"

struct Sample
{
    float3 visibilePosition;
    float3 visibileNormal;
    float3 samplePosition;
    float3 sampleNormal;
    float3 radiance;
};

// Weighted Reservoir Sampling
struct Reservoir
{
    Sample sample;
    float sumWeight;
    float M;
    float W; //average weight
    
    bool Update(Sample s_new, float w_new, float random)
    {
        sumWeight += w_new;
        M += 1;
        
        if (random < w_new / max(sumWeight, 0.00001)) //avoid divide by 0
        {
            sample = s_new;
            return true;
        }
        
        return false;
    }
    
    bool Merge(Reservoir r, float target_pdf, float random)
    {
        float M0 = M;
        bool updated = Update(r.sample, target_pdf * r.W * r.M, random);
        M = M0 + r.M;
        
        return updated;
    }
};

float TargetFunction(float3 radiance)
{
    //Luminance(radiance * brdf * NdotL);
    return Luminance(radiance);
}