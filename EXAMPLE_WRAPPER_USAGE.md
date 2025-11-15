# FBB Wrapper API Usage Examples

This document demonstrates the new C++ wrapper methods that eliminate the need for manual casting when working with embedded FBB messages.

## Array of FBBs

### Old Way (manual casting required)

```cpp
for (size_t i = 0; i < ic_msg->get_file_actions_count(); i++) {
  const FBBCOMM_Serialized *action = ic_msg->get_file_actions_at(i);
  switch (action->get_tag()) {
    case FBBCOMM_TAG_posix_spawn_file_action_open: {
      // Manual cast required here
      const FBBCOMM_Serialized_posix_spawn_file_action_open *action_open =
          reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_open *>(action);
      int flags = action_open->get_flags();
      // ... use action_open
      break;
    }
    case FBBCOMM_TAG_posix_spawn_file_action_dup2: {
      // Manual cast required here
      auto action_dup2 =
          reinterpret_cast<const FBBCOMM_Serialized_posix_spawn_file_action_dup2 *>(action);
      int oldfd = action_dup2->get_oldfd();
      // ... use action_dup2
      break;
    }
  }
}
```

### New Way (automatic casting with wrapper)

```cpp
for (size_t i = 0; i < ic_msg->get_file_actions_count(); i++) {
  const FBBCOMM_Serialized *action = ic_msg->get_file_actions_at(i);
  switch (action->get_tag()) {
    case FBBCOMM_TAG_posix_spawn_file_action_open: {
      // No manual cast needed! Automatic type-safe casting using template
      auto action_open = ic_msg->get_file_actions_as<FBBCOMM_TAG_posix_spawn_file_action_open>(i);
      int flags = action_open->get_flags();
      // ... use action_open
      break;
    }
    case FBBCOMM_TAG_posix_spawn_file_action_dup2: {
      // No manual cast needed! Automatic type-safe casting using template
      auto action_dup2 = ic_msg->get_file_actions_as<FBBCOMM_TAG_posix_spawn_file_action_dup2>(i);
      int oldfd = action_dup2->get_oldfd();
      // ... use action_dup2
      break;
    }
  }
}
```

## Required/Optional FBB Fields

### Old Way (manual casting required)

```cpp
const FBBCOMM_Serialized *embedded = msg->get_reqfbb();
const FBBCOMM_Serialized_specific_type *typed_embedded =
    reinterpret_cast<const FBBCOMM_Serialized_specific_type *>(embedded);
// ... use typed_embedded
```

### New Way (automatic casting with wrapper)

```cpp
auto typed_embedded = msg->get_reqfbb_as<FBBCOMM_TAG_specific_type>();
// ... use typed_embedded
```

## Benefits

1. **No manual casting**: The wrapper methods handle the `reinterpret_cast` automatically
2. **Type-safe**: The tag parameter is checked at compile time
3. **Cleaner code**: Reduces boilerplate and improves readability
4. **Generated code**: These wrappers are in the generated code, so they don't need to be maintained manually
5. **Backward compatible**: Existing code using manual casts continues to work

## Technical Details

The wrapper methods use C++ templates and tag-to-type mapping traits:

```cpp
// Tag-to-type mapping (automatically generated)
template<> struct FBBCOMM_Tag_To_Serialized_Type<FBBCOMM_TAG_foo> {
  using Type = FBBCOMM_Serialized_foo;
};

// Wrapper method for array elements
template<int Tag>
inline const typename FBBCOMM_Tag_To_Serialized_Type<Tag>::Type* 
get_file_actions_as(fbb_size_t idx) const {
  return reinterpret_cast<const typename FBBCOMM_Tag_To_Serialized_Type<Tag>::Type*>(
      get_file_actions_at(idx));
}

// Wrapper method for required/optional fields
template<int Tag>
inline const typename FBBCOMM_Tag_To_Serialized_Type<Tag>::Type* 
get_reqfbb_as() const {
  return reinterpret_cast<const typename FBBCOMM_Tag_To_Serialized_Type<Tag>::Type*>(
      get_reqfbb());
}
```
