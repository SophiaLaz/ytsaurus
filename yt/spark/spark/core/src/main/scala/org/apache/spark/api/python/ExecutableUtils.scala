/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.apache.spark.api.python

import java.io.File

import org.apache.hadoop.fs.Path

import org.apache.spark.{SparkEnv, SparkFiles}

private[spark] object ExecutableUtils {

  // executable must know about python files provided by spark.yt.pyFiles before the process starts,
  // otherwise, if there are top-level imports from those python files, they will fail
  def pythonPath: String = {
    val ytPyFiles = SparkEnv.get.conf.get("spark.yt.pyFiles", "")
    val localPyFiles = ytPyFiles.split(",").map(new Path(_).getName)
    localPyFiles.map(SparkFiles.get).mkString(File.pathSeparator)
  }

  def executablePath: String = {
    val execName = new Path(SparkEnv.get.conf.get("spark.yt.executableResource")).getName
    SparkFiles.get(execName)
  }
}
