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
      // No manual cast needed! Use as_<tagname>() wrapper method
      auto action_open = action->as_posix_spawn_file_action_open();
      int flags = action_open->get_flags();
      // ... use action_open
      break;
    }
    case FBBCOMM_TAG_posix_spawn_file_action_dup2: {
      // No manual cast needed! Use as_<tagname>() wrapper method
      auto action_dup2 = action->as_posix_spawn_file_action_dup2();
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
const FBBCOMM_Serialized *embedded = msg->get_reqfbb();
auto typed_embedded = embedded->as_specific_type();
// ... use typed_embedded
```

## Benefits

1. **No manual casting**: The wrapper methods handle the `reinterpret_cast` automatically
2. **No template parameters**: Simply call `as_<tagname>()` on the generic FBB pointer
3. **Cleaner code**: Reduces boilerplate and improves readability
4. **Generated code**: These wrappers are in the generated code, so they don't need to be maintained manually
5. **Backward compatible**: Existing code using manual casts continues to work

## Technical Details

The wrapper methods are generated for each tag type on the generic FBB pointers:

```cpp
// For FBBCOMM_Serialized (automatically generated for each tag)
struct FBBCOMM_Serialized {
  // ... other methods ...
  
  inline const FBBCOMM_Serialized_foo* as_foo() const {
    return reinterpret_cast<const FBBCOMM_Serialized_foo*>(this);
  }
  
  inline const FBBCOMM_Serialized_bar* as_bar() const {
    return reinterpret_cast<const FBBCOMM_Serialized_bar*>(this);
  }
  
  // ... one as_<tagname>() method for each tag ...
};

// Usage example
const FBBCOMM_Serialized *msg = get_some_fbb();
auto specific = msg->as_foo();  // No template parameter needed!
```
