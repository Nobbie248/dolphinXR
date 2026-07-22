// SPDX-License-Identifier: GPL-2.0-or-later

package org.dolphinemu.dolphinemu.dialogs

import android.app.Dialog
import android.os.Bundle
import androidx.fragment.app.DialogFragment
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.dolphinemu.dolphinemu.R
import org.dolphinemu.dolphinemu.utils.FirstLaunchDialogs

class QuestControllerSetupDialog : DialogFragment() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        isCancelable = false
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        return MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.quest_controller_setup_title)
            .setMessage(R.string.quest_controller_setup_description)
            .setPositiveButton(R.string.yes) { _, _ ->
                FirstLaunchDialogs.onQuestControllerSetupAnswered(requireActivity(), true)
            }
            .setNegativeButton(R.string.no) { _, _ ->
                FirstLaunchDialogs.onQuestControllerSetupAnswered(requireActivity(), false)
            }
            .create()
    }

    companion object {
        const val TAG = "QuestControllerSetupDialog"
    }
}
