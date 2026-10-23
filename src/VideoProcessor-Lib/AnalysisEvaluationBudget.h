#pragma once
#include <exception>

// Explicit, caller-owned optional-work budget. No clock reads or cancellation
// occur on mandatory nullptr paths. Confine each token to its evaluating thread.
class AnalysisEvaluationCancelled final : public std::exception
{
public:
    const char* what() const noexcept override { return "optional analysis budget exhausted"; }
};

class AnalysisEvaluationBudget
{
public:
    using Expired = bool (*)(void* context);
    AnalysisEvaluationBudget(Expired expired, void* context)
        : m_expired(expired), m_context(context) {}

    void Checkpoint() const
    {
        if (m_cancelled || (m_expired && m_expired(m_context)))
        {
            m_cancelled = true;
            throw AnalysisEvaluationCancelled{};
        }
    }
    bool Cancelled() const noexcept { return m_cancelled; }

private:
    Expired m_expired;
    void* m_context;
    mutable bool m_cancelled = false;
};

inline void CheckAnalysisEvaluationBudget(const AnalysisEvaluationBudget* budget)
{
    if (budget) budget->Checkpoint();
}
