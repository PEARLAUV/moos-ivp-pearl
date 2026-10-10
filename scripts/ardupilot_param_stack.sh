#!/usr/bin/env bash
# Source-only profile selection shared by mission launchers. No FC commands.
# Defaults may be absent. Explicit nonempty paths must exist; empty/none omits.
ardupilot_parameter_stack() {
  local root=$1 mission=$2 mode=$3 name value default_path index
  local model=${DRONE_MODEL-x500}
  ARDUPILOT_PARAM_ARGS=()
  # A basename selector, not a path, FC identity or SITL physics/frame option.
  if [[ ! "$model" =~ ^[A-Za-z0-9][A-Za-z0-9_-]*$ ]]; then
    echo "Invalid drone model: $model (use a profile basename without .parm)" >&2
    return 1
  fi
  default_path="$root/config/ardupilot/drones/$model.parm"
  # Explicit model selection must not silently omit a missing hardware layer.
  # An explicit file/none override wins regardless of argument order.
  if [[ -v DRONE_MODEL && ! -v DRONE_PARAM_FILE && ! -f "$default_path" ]]; then
    echo "Drone model profile not found: $default_path" >&2
    return 1
  fi
  DRONE_MODEL=$model
  export DRONE_MODEL
  local names=(DRONE_PARAM_FILE MODE_PARAM_FILE MISSION_PARAM_FILE)
  local defaults=("$default_path" \
                  "$mission/${mode,,}.parm" "$mission/mission.parm")
  for index in "${!names[@]}"; do
    name=${names[$index]}
    default_path=${defaults[$index]}
    if [[ -v "$name" ]]; then
      value=${!name}
      if [[ -z "$value" || "$value" == none ]]; then
        printf -v "$name" '%s' ''
        export "$name"
        continue
      fi
      if [[ ! -f "$value" ]]; then
        echo "Explicit $name is not a parameter file: $value" >&2
        return 1
      fi
    elif [[ -f "$default_path" ]]; then
      value=$default_path
    else
      value=''
    fi
    if [[ -n "$value" ]]; then
      value="$(cd -- "$(dirname -- "$value")" && pwd -P)/$(basename -- "$value")" || return 1
      ARDUPILOT_PARAM_ARGS+=(--param-file "$value")
    fi
    printf -v "$name" '%s' "$value"
    export "$name"
  done
}
