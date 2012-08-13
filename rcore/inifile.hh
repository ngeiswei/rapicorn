// Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html
#ifndef __RAPICORN_INIFILE_HH__
#define __RAPICORN_INIFILE_HH__

#include <rcore/utilities.hh>

namespace Rapicorn {

class IniFile : protected NonCopyable {
  typedef std::map<String,StringVector> SectionMap;
  SectionMap                    m_sections;
  void          load_ini        (const String &inputname, const String &data);
  //bool        set             (const String &section, const String &key, const String &value, const String &locale = "");
  //bool        del             (const String &section, const String &key, const String &locale = "*");
  //bool        value           (const String &dotpath, const String &value);
  const StringVector& section   (const String &name) const;
public:
  explicit      IniFile         (const String &res_ini);        ///< Construct IniFile from INI resource blob.
  explicit      IniFile         (const String &ini_string,int); ///< Construct IniFile from INI string.
  //String      get             (const String &section, const String &key, const String &locale = "") const;
  StringVector  sections        () const;                       ///< List all sections.
  bool          has_section     (const String &section) const;  ///< Check presence of a section.
  StringVector  attributes      (const String &section) const;  ///< List all attributes available in @a section.
  bool          has_attribute   (const String &section, const String &key) const; ///< Return if @a section contains @a key.
  String        raw_value       (const String &dotpath) const;  ///< Retrieve raw (uncooked) value of section.attribute[locale].
  StringVector  raw_values      () const;                       ///< List all section.attribute=value pairs.
  String        value_as_string (const String &dotpath) const;  ///< Retrieve value of section.attribute[locale].
  static String cook_string     (const String &input_string);   ///< Unquote contents of @a input_string;
};

} // Rapicorn

#endif /* __RAPICORN_INIFILE_HH__ */
