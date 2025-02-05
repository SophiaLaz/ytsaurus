#pragma once

#include <yt/cpp/roren/interface/type_tag.h>
#include <yt/cpp/roren/interface/private/raw_pipeline.h>
#include <util/generic/fwd.h>
#include <tuple>
#include <concepts>
#include <optional>

template <typename T>
concept is_optional = std::same_as<T, std::optional<typename T::value_type>>;

namespace NRoren {

////////////////////////////////////////////////////////////////////////////////

namespace NPrivate {
struct TYtStateVtable
{
    template <class TState>
    using TStateTKV = TKV<typename TState::TKey, typename TState::TValue>;
    using TLoadState = void (*)(TRawRowHolder& row, const NYT::TNode&);  // NYT::TNode->TStateTKV
    using TSaveState = void (*)(::NYson::TYsonWriter& writer, void* rawState, const void* rawTKV); // TState -> Cout
    using TStateFromKey = TRawRowHolder (*)(const void* rawKey); // TState::TKey -> TState
    using TStateFromTKV = TRawRowHolder (*)(const void* rawTKV); // TVK<TState::TKey, TState::TValue> -> TState

    TRowVtable StateTKVvtable;
    TLoadState LoadState = nullptr;
    TSaveState SaveState = nullptr;
    TStateFromKey StateFromKey = nullptr;
    TStateFromTKV StateFromTKV = nullptr;

    static NYT::TNode SerializableToNode(const TYtStateVtable& stateVtable);
    static TYtStateVtable SerializableFromNode(const NYT::TNode& node);
}; // struct YtStateVtable

extern TTypeTag<TYtStateVtable> YtStateVtableTag;
extern TTypeTag<TString> YtStateInPathTag;
extern TTypeTag<TString> YtStateOutPathTag;

Y_HAS_MEMBER(Base);

template <class T, std::enable_if_t<THasBase<T>::value, int> = 0>
inline TStringBuf Name(const T& column) {
    return column.Base();
}

template <class T, std::enable_if_t<!THasBase<T>::value, int> = 0>
inline TStringBuf Name(const T& column) {
    return column.Name();
}

template <class TColumnNames, class T>
inline void LoadStateItem(T& dst, size_t i, const TColumnNames& columns, NYT::TNode node)
{
    auto column = Name(columns[i]);
    if (!node.HasKey(column)) {
        return;
    }
    const auto& data = node[column];
    if (data.IsNull()) {
        return;
    }

    if constexpr (is_optional<T>) {
        dst = data.template As<typename T::value_type>();
    } else {
        dst = data.template As<T>();
    }
}

template <class TColumnNames, class TItemsPack>
inline void LoadState(TItemsPack& vp, const TColumnNames& columns, NYT::TNode node)
{
    auto f = [&] (auto& ... v) {
        size_t i = 0;
        (LoadStateItem(v, i++, columns, node), ...);
    };
    std::apply(f, vp);
}


template <class TState>
void LoadStateEntry(TRawRowHolder& row, const NYT::TNode& node)
{
    LoadState(*static_cast<typename TState::TKey*>(row.GetKeyOfKV()), TState::Schema().KeyColumns, node);
    LoadState(*static_cast<typename TState::TValue*>(row.GetValueOfKV()), TState::Schema().ValueColumns, node);
}

template <typename T>
void unsupported_type(const T&) = delete;

template <class TColumnNames, class T>
inline void SaveStateItem(::NYson::TYsonWriter& writer, const T& item, size_t i, const TColumnNames& columns)
{
    if constexpr (is_optional<T>) {
        if (item) {
            SaveStateItem(writer, item.value(), i, columns);
        }
    } else {
        const auto& columnName = Name(columns[i]);
        writer.OnKeyedItem(columnName);
        if constexpr (std::is_same_v<T, bool>) {
            writer.OnBooleanScalar(item);
        } else if constexpr (std::is_same_v<T, TString> || std::is_same_v<T, TStringBuf>) {
            writer.OnStringScalar(item);
        } else if constexpr (std::is_same_v<T, i64>) {
            writer.OnInt64Scalar(item);
        } else if constexpr (std::is_same_v<T, ui64>) {
            writer.OnUint64Scalar(item);
        } else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
            writer.OnDoubleScalar(item);
        } else {
            unsupported_type(item);
        }
    }
}

template <class TColumnNames, class TItemsPack>
inline void SaveState(::NYson::TYsonWriter& writer, const TColumnNames& columns, const TItemsPack& ip)
{
    auto f = [&] (auto& ... item) {
        size_t i = 0;
        (SaveStateItem(writer, item, i++, columns), ...);
    };
    std::apply(f, ip);
}

template <class T>
inline void UpdateValue(T& value, const std::optional<T>& update)
{
    if (update) {
        value = update.value();
    }
}

template <typename TValues, typename TMutationValues, std::size_t... I>
inline void UpdateValues(TValues& result, const TMutationValues& update, std::index_sequence<I...>)
{
    (UpdateValue(std::get<I>(result), std::get<I>(update)), ...);
}

template <typename TValues, typename TMutationValues>
inline void UpdateValues(TValues& result, const TMutationValues& update)
{
    auto seq = std::make_index_sequence<std::tuple_size_v<TValues>>{};
    UpdateValues(result, update, seq);
}

template <class TState>
inline void SaveStateEntry(::NYson::TYsonWriter& writer, void* rawState, const void* rawTKV)
{
    const auto* tkv = reinterpret_cast<const TYtStateVtable::TStateTKV<TState>*>(rawTKV);
    TState* state = static_cast<TState*>(rawState);
    if (state->IsEmpty()) {
        return;
    }

    auto mutation = state->Flush();
    if (mutation.IsClearing) {
        return;
    }

    typename TState::TValue values = tkv->Value();
    UpdateValues(values, mutation.Value);

    writer.OnListItem();
    writer.OnBeginMap();
    SaveState(writer, TState::Schema().KeyColumns, state->GetKey());
    SaveState(writer, TState::Schema().ValueColumns, values);
    writer.OnEndMap();
}

template <class TState>
TRawRowHolder StateFromKey(const void* rawKey)
{
    const auto* key = reinterpret_cast<const typename TState::TKey*>(rawKey);
    TRawRowHolder result(MakeRowVtable<TState>());
    TState& state = *reinterpret_cast<TState*>(result.GetData());
    state = TState(*key);
    return result;
}

template <class TState>
TRawRowHolder StateFromTVK(const void* rawTKV)
{
    const auto* tkv = reinterpret_cast<const TYtStateVtable::TStateTKV<TState>*>(rawTKV);
    TRawRowHolder result(MakeRowVtable<TState>());
    TState& state = *reinterpret_cast<TState*>(result.GetData());
    state = TState(tkv->Key(), typename TState::TValue(tkv->Value()));
    return result;
}

}  // namespace NPrivate

////////////////////////////////////////////////////////////////////////////////

template <typename TState>
TPState<typename TState::TKey, TState> MakeYtProfilePState(const TPipeline& YtPipeline, TString in_state_path, TString out_state_path = {})
{
    using NPrivate::TRowVtable;
    using NPrivate::TRawRowHolder;
    using NPrivate::MakeRowVtable;
    using NPrivate::TYtStateVtable;
    using NPrivate::LoadStateEntry;
    using NPrivate::SaveStateEntry;
    using NPrivate::StateFromKey;
    using NPrivate::StateFromTVK;

    if (out_state_path.empty()) {
        out_state_path = in_state_path;
    }
    auto rawPipeline = NPrivate::GetRawPipeline(YtPipeline);
    auto pState = NPrivate::MakePState<typename TState::TKey, TState>(rawPipeline);
    auto rawPState = NPrivate::GetRawPStateNode(pState);

    NPrivate::TYtStateVtable stateVtable;
    stateVtable.StateTKVvtable = MakeRowVtable<TYtStateVtable::TStateTKV<TState>>();
    stateVtable.LoadState = &LoadStateEntry<TState>;
    stateVtable.SaveState = &SaveStateEntry<TState>;
    stateVtable.StateFromKey = &StateFromKey<TState>;
    stateVtable.StateFromTKV = &StateFromTVK<TState>;
    NPrivate::SetAttribute(*rawPState, NPrivate::YtStateVtableTag, std::move(stateVtable));
    NPrivate::SetAttribute(*rawPState, NPrivate::YtStateInPathTag, std::move(in_state_path));
    NPrivate::SetAttribute(*rawPState, NPrivate::YtStateOutPathTag, std::move(out_state_path));
    return pState;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NRoren

template <>
class TSerializer<NRoren::NPrivate::TYtStateVtable>
{
public:
    using TYtStateVtable = NRoren::NPrivate::TYtStateVtable;

public:
    static void Save(IOutputStream* output, const TYtStateVtable& stateVtable);
    static void Load(IInputStream* input, TYtStateVtable& stateVtable);
};

