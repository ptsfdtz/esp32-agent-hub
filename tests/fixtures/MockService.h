#pragma once
#include "models/Model.h"
class MockService {
public:
    void begin(Model& model, uint32_t now);
    void update(Model& model, uint32_t now);
    void changeSample(Model& model, uint32_t now);
private:
    uint32_t heartbeat_ = 0;
    bool alternate_ = false;
};
