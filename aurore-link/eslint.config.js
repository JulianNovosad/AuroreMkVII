import pluginSecurity from "eslint-plugin-security";
import globals from "globals";
import js from "@eslint/js";

export default [
  js.configs.recommended,
  pluginSecurity.configs.recommended,
  {
    languageOptions: {
      ecmaVersion: 2022,
      sourceType: "module",
      globals: {
        ...globals.browser,
        ...globals.node,
      },
    },
    rules: {
      "no-unused-vars": "warn",
      "no-console": "off",
    },
  },
];
