/*
Flipper is a recommendation and search engine for fanfiction.net
Copyright (C) 2017-2020  Marchenko Nikolai

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>
*/
#pragma once
#include "rec_calc/rec_calculator_weighted.h"
#include "data_code/data_holders.h"
#include "data_code/rec_calc_data.h"
#include <array>

namespace core {

class RecCalculatorImplMoodAdjusted: public RecCalculatorImplWeighted{
public:
    RecCalculatorImplMoodAdjusted(const RecInputVectors& input, const genre_stats::GenreMoodData& moodData);
    std::optional<double> GetNeutralDiffForLists(uint32_t) override;
    std::optional<double> GetTouchyDiffForLists(uint32_t) override;
    virtual FilterListType GetFilterList();

    QHash<uint32_t, ListMoodDifference> moodDiffs;
    genre_stats::GenreMoodData moodData;


    void ResetAccumulatedData() override;
    bool WeightingIsValid() const override;
};


}
