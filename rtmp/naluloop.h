#ifndef NALULOOP_H
#define NALULOOP_H

#include "looper.h"

namespace LQF
{
    class NaluLoop : public Looper
    {
    public:
        explicit NaluLoop(int queue_nalu_len);

    private:
        void addmsg(LooperMessage* msg, bool flush) override;

    private:
        int max_nalu_;
    };
}
#endif // NALULOOP_H
