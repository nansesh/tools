#!/bin/bash

if [ ! -f $1 ]
  then
  echo "input lib file does not exist"
  exit 1
fi

build_defs() {
  def_table_name="t_defs"
  def_table_cmd="CREATE TABLE t_defs (func text, objfile text); CREATE INDEX i_defs_func on t_defs(func);"
  undef_table_name="t_undefs"
  undef_table_cmd="CREATE TABLE t_undefs (func text, objfile text); CREATE INDEX i_undefs_objfile on t_undefs(objfile);"
  tn=$def_table_name
  tcmd=$def_table_cmd
  nm_flag=-oU
  func_substr_index=18
  if [ "$3" == "undefs" ]; then
    tn=$undef_table_name
    tcmd=$undef_table_cmd
    nm_flag=-ou
    func_substr_index=0
  fi

  # echo "../a/b/g.lib:spawn.o: __thisisafunc@24"
  nm $nm_flag $1 \
   | awk -v tc="$tcmd" -v tname="$tn" -v fname_index=$func_substr_index 'BEGIN {bcount = 0; FS=":"; print tc;
   }
   {
    gsub(/ /,"",$2);
    gsub(/ /,"",$3);
    $3 = substr($3, fname_index);
    bcount = bcount + 1;
    if (bcount == 1) {
      print "BEGIN TRANSACTION; insert into "tname" select \""$3"\" as func, \""$2"\" as objfile";
    }
    else if (bcount < 256) {
      print "union select \""$3"\", \""$2"\"";
    }
    else {
     print "; END TRANSACTION; ";
     bcount = 0;
    }
    }
    END {print "; END TRANSACTION;"}'\
    | sqlite3 $2
  }

build_db() {
  build_defs $1 $2 "defs"
  build_defs $1 $2 "undefs"
}

inp_lib=${1##*/}
if [ -f $inp_lib.db ]; then
  echo "lib dependency database exists. Do you want to rebuild it?(y/n):"
  read yn
  if [ "$yn" == "y" ]
    then
    rm -f $inp_lib.db
    echo "rebuilding..."
    build_db $1 $inp_lib.db
  fi
else
  echo "building the lib dep database $inp_lib.db"
  build_db $1 $inp_lib.db
fi

exit 0

# | wc -l
# gsub(/\/.*\//,"",$1);