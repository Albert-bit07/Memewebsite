#pragma once
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <vector>

// Interaction membership is owned by the caller; duplicate likes must not reach add().
class Preference {
public:
    explicit Preference(std::size_t dimension) : sum_(dimension, 0.0) {}
    void add(const std::vector<float>& embedding) { update(embedding, 1); ++count_; }
    void remove(const std::vector<float>& embedding) {
        if (!count_) throw std::logic_error("Cannot remove from an empty preference");
        update(embedding, -1);
        if (--count_ == 0) std::fill(sum_.begin(), sum_.end(), 0.0);
    }
    std::vector<float> vector() const {
        double norm = 0;
        for (auto value : sum_) norm += value * value;
        norm = std::sqrt(norm);
        std::vector<float> result(sum_.size(), 0);
        if (norm > 0) for (std::size_t i = 0; i < sum_.size(); ++i) result[i] = sum_[i] / norm;
        return result;
    }
private:
    void update(const std::vector<float>& embedding, double sign) {
        if (embedding.size() != sum_.size()) throw std::invalid_argument("Preference dimension mismatch");
        for (float value : embedding) if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite preference input");
        for (std::size_t i = 0; i < sum_.size(); ++i) sum_[i] += sign * embedding[i];
    }
    std::vector<double> sum_;
    std::size_t count_ = 0;
};
